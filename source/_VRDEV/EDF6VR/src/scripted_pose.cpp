#include "scripted_pose.h"
#include "openxr_session.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace edf6vr {
namespace {

struct Vector { float v[3]{}; bool given=false; };
struct Turn { float q[4]{0,0,0,1}; bool given=false; };
struct Script {
    Vector head,hand[2];
    Turn headTurn,handTurn[2];
    float trigger[2]{};  bool triggerGiven[2]{};
    float squeeze[2]{};  bool squeezeGiven[2]{};
    float stick[2][2]{}; bool stickGiven[2]{};
    // Buttons, so a menu can be walked as well as a hand placed.
    bool lower[2]{},upper[2]{},menu[2]{},click[2]{};
    bool lowerGiven[2]{},upperGiven[2]{},menuGiven[2]{},clickGiven[2]{};
    char note[128]="";
};

wchar_t g_path[MAX_PATH]{};
bool g_armed=false;
Script g_script{};
SRWLOCK g_lock=SRWLOCK_INIT;
ULONGLONG g_read=0;
FILETIME g_stamp{};

bool Number(const char* text,float& out) noexcept {
    char* end=nullptr;
    const double value=std::strtod(text,&end);
    if(end==text) return false;
    out=static_cast<float>(value);
    return true;
}

// "a, b, c" -- as many as are there, the rest left alone.
int Numbers(const char* text,float* out,int most) noexcept {
    int found=0;
    const char* at=text;
    while(found<most && *at) {
        while(*at==' ' || *at=='\t' || *at==',') ++at;
        if(!*at) break;
        char* end=nullptr;
        const double value=std::strtod(at,&end);
        if(end==at) break;
        out[found++]=static_cast<float>(value);
        at=end;
    }
    return found;
}

// Pitch, yaw and roll in degrees, in the order the rest of this mod uses them.
void TurnFromDegrees(const float degrees[3],float q[4]) noexcept {
    constexpr float kToRadians=3.14159265358979f/180.0f;
    const float p=degrees[0]*kToRadians*0.5f;
    const float y=degrees[1]*kToRadians*0.5f;
    const float r=degrees[2]*kToRadians*0.5f;
    const float cp=std::cos(p), sp=std::sin(p);
    const float cy=std::cos(y), sy=std::sin(y);
    const float cr=std::cos(r), sr=std::sin(r);
    q[0]=sp*cy*cr+cp*sy*sr;
    q[1]=cp*sy*cr-sp*cy*sr;
    q[2]=cp*cy*sr-sp*sy*cr;
    q[3]=cp*cy*cr+sp*sy*sr;
}

void Take(const char* key,const char* value,Script& into) noexcept {
    auto vector=[&](Vector& slot) {
        float v[3]{};
        if(Numbers(value,v,3)==3) { for(int j=0;j<3;++j) slot.v[j]=v[j]; slot.given=true; }
    };
    auto turn=[&](Turn& slot) {
        float d[3]{};
        if(Numbers(value,d,3)==3) { TurnFromDegrees(d,slot.q); slot.given=true; }
    };
    auto scalar=[&](float& slot,bool& given) {
        float v=0;
        if(Number(value,v)) { slot=v; given=true; }
    };
    if(!_stricmp(key,"note")) { strncpy_s(into.note,value,_TRUNCATE); return; }
    if(!_stricmp(key,"hmd")) return vector(into.head);
    if(!_stricmp(key,"hmdRot")) return turn(into.headTurn);
    if(!_stricmp(key,"left")) return vector(into.hand[0]);
    if(!_stricmp(key,"right")) return vector(into.hand[1]);
    if(!_stricmp(key,"leftRot")) return turn(into.handTurn[0]);
    if(!_stricmp(key,"rightRot")) return turn(into.handTurn[1]);
    if(!_stricmp(key,"triggerL")) return scalar(into.trigger[0],into.triggerGiven[0]);
    if(!_stricmp(key,"triggerR")) return scalar(into.trigger[1],into.triggerGiven[1]);
    if(!_stricmp(key,"gripL")) return scalar(into.squeeze[0],into.squeezeGiven[0]);
    if(!_stricmp(key,"gripR")) return scalar(into.squeeze[1],into.squeezeGiven[1]);
    auto flag=[&](bool* slot,bool* given,int hand) {
        float v=0;
        if(Number(value,v)) { slot[hand]=v>0.5f; given[hand]=true; }
    };
    if(!_stricmp(key,"aL")) return flag(into.lower,into.lowerGiven,0);
    if(!_stricmp(key,"aR")) return flag(into.lower,into.lowerGiven,1);
    if(!_stricmp(key,"bL")) return flag(into.upper,into.upperGiven,0);
    if(!_stricmp(key,"bR")) return flag(into.upper,into.upperGiven,1);
    if(!_stricmp(key,"menuL")) return flag(into.menu,into.menuGiven,0);
    if(!_stricmp(key,"menuR")) return flag(into.menu,into.menuGiven,1);
    if(!_stricmp(key,"clickL")) return flag(into.click,into.clickGiven,0);
    if(!_stricmp(key,"clickR")) return flag(into.click,into.clickGiven,1);
    if(!_stricmp(key,"stickL") || !_stricmp(key,"stickR")) {
        const int hand=(key[5]=='L' || key[5]=='l')?0:1;
        float v[2]{};
        if(Numbers(value,v,2)==2) {
            into.stick[hand][0]=v[0]; into.stick[hand][1]=v[1];
            into.stickGiven[hand]=true;
        }
    }
}

// Re-read at most once a second, and only when the file has actually changed,
// so a script can be rewritten mid-session without restarting anything.
void Refresh() noexcept {
    if(!g_armed) return;
    const auto now=GetTickCount64();
    if(now-g_read<1000) return;
    g_read=now;
    WIN32_FILE_ATTRIBUTE_DATA about{};
    if(!GetFileAttributesExW(g_path,GetFileExInfoStandard,&about)) return;
    if(about.ftLastWriteTime.dwLowDateTime==g_stamp.dwLowDateTime
       && about.ftLastWriteTime.dwHighDateTime==g_stamp.dwHighDateTime) return;
    g_stamp=about.ftLastWriteTime;
    HANDLE file=CreateFileW(g_path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                            OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    char text[4096]{};
    DWORD got=0;
    const bool read=ReadFile(file,text,sizeof(text)-1,&got,nullptr)!=0;
    CloseHandle(file);
    if(!read) return;
    text[got]=0;
    Script fresh{};
    char* line=text;
    while(line && *line) {
        char* nextLine=std::strpbrk(line,"\r\n");
        if(nextLine) { *nextLine=0; ++nextLine; while(*nextLine=='\r' || *nextLine=='\n') ++nextLine; }
        char* hash=std::strpbrk(line,"#;");
        if(hash) *hash=0;
        char* equals=std::strchr(line,'=');
        if(equals) {
            *equals=0;
            char* key=line;
            char* value=equals+1;
            while(*key==' ' || *key=='\t') ++key;
            for(char* end=key+std::strlen(key);end>key && (end[-1]==' '||end[-1]=='\t');--end) end[-1]=0;
            while(*value==' ' || *value=='\t') ++value;
            Take(key,value,fresh);
        }
        line=nextLine;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_script=fresh;
    ReleaseSRWLockExclusive(&g_lock);
}

}   // namespace

void ArmScriptedPoses(const wchar_t* path) noexcept {
    if(!path || !path[0]) { g_armed=false; return; }
    wcsncpy_s(g_path,path,_TRUNCATE);
    g_armed=true;
    g_read=0;
    g_stamp=FILETIME{};
    Refresh();
}

bool ScriptedPosesArmed() noexcept { return g_armed; }

const char* ScriptedPoseNote() noexcept { return g_script.note; }

void ScriptHead(HmdSample& sample) noexcept {
    if(!g_armed) return;
    Refresh();
    AcquireSRWLockShared(&g_lock);
    const Script script=g_script;
    ReleaseSRWLockShared(&g_lock);
    if(script.head.given) {
        sample.position=Vec3{script.head.v[0],script.head.v[1],script.head.v[2]};
        sample.positionValid=true;
    }
    if(script.headTurn.given) {
        sample.orientation=Quat{script.headTurn.q[0],script.headTurn.q[1],
                                script.headTurn.q[2],script.headTurn.q[3]};
        sample.orientationValid=true;
    }
}

bool ScriptHand(int hand,float position[3],float orientation[4],
                bool& gavePosition,bool& gaveTurn) noexcept {
    gavePosition=false;
    gaveTurn=false;
    if(!g_armed || hand<0 || hand>1) return false;
    Refresh();
    AcquireSRWLockShared(&g_lock);
    const Script script=g_script;
    ReleaseSRWLockShared(&g_lock);
    if(script.hand[hand].given) {
        for(int j=0;j<3;++j) position[j]=script.hand[hand].v[j];
        gavePosition=true;
    }
    if(script.handTurn[hand].given) {
        for(int j=0;j<4;++j) orientation[j]=script.handTurn[hand].q[j];
        gaveTurn=true;
    }
    return gavePosition || gaveTurn;
}

void ScriptControls(ControllerState& controls) noexcept {
    if(!g_armed) return;
    Refresh();
    AcquireSRWLockShared(&g_lock);
    const Script script=g_script;
    ReleaseSRWLockShared(&g_lock);
    for(int i=0;i<2;++i) {
        // A scripted hand is a present hand, or nothing downstream would look
        // at it: half the mod asks "is this hand tracked?" before anything else,
        // and the whole point is to work with no controller in the room.
        if(script.hand[i].given || script.handTurn[i].given) controls.present[i]=true;
        if(script.triggerGiven[i]) controls.trigger[i]=script.trigger[i];
        if(script.squeezeGiven[i]) controls.squeeze[i]=script.squeeze[i];
        if(script.stickGiven[i]) {
            controls.stick[i][0]=script.stick[i][0];
            controls.stick[i][1]=script.stick[i][1];
        }
        if(script.lowerGiven[i]) controls.lower[i]=script.lower[i];
        if(script.upperGiven[i]) controls.upper[i]=script.upper[i];
        if(script.menuGiven[i]) controls.menu[i]=script.menu[i];
        if(script.clickGiven[i]) controls.stickClick[i]=script.click[i];
    }
}

}   // namespace edf6vr
