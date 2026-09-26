#include "../src/plugin.cpp"
static int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
static void* expectedPlayer=reinterpret_cast<void*>(0x1234);
static unsigned panCalls=0,volumeCalls=0,spreadCalls=0;
static float lastPan=0,lastVolume=0,lastSpread=0;
static unsigned statusCalls=0,managerCalls=0;
static int rawStatus=2;
static void* lifeVoice=nullptr;
static void* lifeManager=nullptr;
static int __fastcall PlaybackStatus(unsigned id) {CHECK(id==123);++statusCalls;return rawStatus;}
static void __fastcall ManagerUpdate(void* manager,float dt) {
    CHECK(manager==lifeManager && dt==.016f);++managerCalls;
    CHECK(HookSoundAlive(lifeVoice)==(rawStatus!=3));
}
static void __fastcall Pan(void* player,float x) {CHECK(player==expectedPlayer);lastPan=x;++panCalls;}
static void __fastcall Volume(void* player,float x) {CHECK(player==expectedPlayer);lastVolume=x;++volumeCalls;}
static void __fastcall Spread(void* player,float x) {CHECK(player==expectedPlayer);lastSpread=x;++spreadCalls;}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason)))return 2;
    // Run the actual EDF spatial calculation, replacing only the three CRI
    // output setters and two math imports. No audio engine or game is started.
    bool changed=false;
    auto atanSlot=reinterpret_cast<void**>(g_image.base+0x1756450);
    CHECK(edf6vr::ReplacePointer(atanSlot,*atanSlot,reinterpret_cast<void*>(static_cast<float(*)(float,float)>(&std::atan2)),changed));
    auto sqrtSlot=reinterpret_cast<void**>(g_image.base+0x1756438);
    CHECK(edf6vr::ReplacePointer(sqrtSlot,*sqrtSlot,reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::sqrt)),changed));
    CHECK(InstallSoundPositionProbe());CHECK(!InstallSoundPositionProbe());
    g_soundPan=&Pan;g_soundVolume=&Volume;
    CHECK(edf6vr::RedirectCall(g_image.base+0x7ACF4B,g_image.base+0xFE7688,reinterpret_cast<void*>(&Spread),changed));
    alignas(16) unsigned char manager[0x70]{},listener[0x50]{};
    alignas(16) float pos[8]={101,22,30,1,2,20,0,0};
    auto view=reinterpret_cast<edf6vr::Matrix*>(listener+0x10);
    for(unsigned i=0;i<4;++i)view->m[i][i]=1;
    auto centre=reinterpret_cast<float*>(listener);
    centre[0]=100;centre[1]=20;centre[2]=30;centre[3]=1;
    for(unsigned i=0;i<3;++i)view->m[3][i]=-centre[i];
    *reinterpret_cast<void**>(manager+0x58)=listener;
    *reinterpret_cast<std::uint64_t*>(manager+0x68)=1;
    unsigned char savedManager[sizeof(manager)],savedListener[sizeof(listener)];float savedPos[8];
    std::memcpy(savedManager,manager,sizeof(manager));std::memcpy(savedListener,listener,sizeof(listener));
    std::memcpy(savedPos,pos,sizeof(pos));
    g_fencerProbeAt=GetTickCount64();g_vrAudioAt=GetTickCount64();g_soundViewMode=3;
    SoundPositionSample outer{},s{};g_soundSample=&outer;
    CHECK(ReadSoundPosition(manager,pos,s));CHECK(s.local[0]==1 && s.local[1]==2 && s.local[2]==0);
    CHECK(s.vr && s.fps);
    HookSpatialSound(manager,expectedPlayer,pos,.75f);
    CHECK(g_soundSample==&outer);CHECK(!outer.havePan); // nested scope restored
    CHECK(panCalls==1 && volumeCalls==1 && spreadCalls==1);
    CHECK(std::fabs(lastPan+90.f)<.001f && std::fabs(lastVolume-.75f)<.001f);
    CHECK(!std::memcmp(savedManager,manager,sizeof(manager)) && !std::memcmp(savedListener,listener,sizeof(listener))
        && !std::memcmp(savedPos,pos,sizeof(pos)));
    // Horizontal source crossing changes native pan; pure Y does not. This is
    // an offline property of the calculation, NOT proof of the user's cause.
    pos[1]+=3;HookSpatialSound(manager,expectedPlayer,pos,.75f);
    CHECK(std::fabs(lastPan+90.f)<.001f);
    pos[0]-=2;HookSpatialSound(manager,expectedPlayer,pos,.75f);
    CHECK(std::fabs(lastPan-90.f)<.001f);
    g_vrAudioAt=0;HookSpatialSound(manager,expectedPlayer,pos,.75f);
    CHECK(panCalls==4 && volumeCalls==4 && spreadCalls==4);CHECK(!outer.havePan);
    *reinterpret_cast<std::uint64_t*>(manager+0x68)=2;CHECK(!ReadSoundPosition(manager,pos,s));
    CHECK(!ReadSoundPosition(nullptr,pos,s));
    *reinterpret_cast<std::uint64_t*>(manager+0x68)=1;
    pos[0]=100.05f;pos[1]=21;pos[4]=0;
    std::memcpy(savedPos,pos,sizeof(pos));
    g_fencerProbeAt=GetTickCount64();g_vrAudioAt=GetTickCount64();g_soundViewMode=3;
    g_soundNearFieldHold=0.5f;g_soundNearFieldFade=2.0f;g_soundPanStep=0;
    // What EDF makes of this source on its own, to compare against.
    g_soundPositionFix=false;HookSpatialSound(manager,expectedPlayer,pos,.75f);
    const float nativePan=lastPan,nativeSpread=lastSpread;
    CHECK(nativePan!=0.f);
    g_soundPositionFix=true;
    const auto beforeCalls=panCalls;
    HookSpatialSound(manager,expectedPlayer,pos,.75f);
    CHECK(panCalls==beforeCalls+1 && volumeCalls==panCalls && spreadCalls==panCalls);
    CHECK(std::fabs(lastVolume-.75f)<.001f);
    CHECK(lastSpread==nativeSpread);               // EDF's own inner radius is passed through
    CHECK(!std::memcmp(savedPos,pos,sizeof(pos))); // and the position itself is never edited
    CHECK(lastPan==0.f);                           // five centimetres away: no direction to hear
    // The fade against the horizontal distance in the listener's own frame,
    // which is the pair the azimuth is built from and the pair that crosses zero.
    SoundPositionSample close{};close.vr=close.fps=true;
    close.local[0]=0.25f;CHECK(NearFieldPan(close,170.f)==0.f);
    close.local[0]=2.5f;CHECK(NearFieldPan(close,170.f)==170.f);
    close.local[0]=1.25f;CHECK(std::fabs(NearFieldPan(close,170.f)-85.f)<.001f);
    // The step this exists to remove: either side of the crossing goes to the
    // front, so there is no jump left between them.
    close.local[0]=0.1f;CHECK(NearFieldPan(close,-174.f)==0.f && NearFieldPan(close,179.f)==0.f);
    close.local[0]=1.25f;close.vr=false;CHECK(NearFieldPan(close,170.f)==170.f);  // VR off untouched
    close.vr=true;close.fps=false;CHECK(NearFieldPan(close,170.f)==170.f);        // third person untouched
    close.fps=true;
    // Only a voice that has done the impossible stops being placed, and it is
    // walked to the front rather than snapped there.
    g_soundPanStep=25.f;g_soundPanFlip=120.f;g_soundPanFlipCount=1;
    const void* steady=reinterpret_cast<const void*>(0xA1);
    CHECK(SteadyPan(steady,10.f,1000)==10.f);            // first sample passes
    CHECK(SteadyPan(steady,20.f,1016)==20.f);            // ordinary motion is untouched
    CHECK(SteadyPan(steady,-60.f,1032)==-60.f);          // 80 degrees is possible, so untouched
    CHECK(SteadyPan(steady,-45.f,1048)==-45.f);          // and nothing is ever slowed down
    const void* flips=reinterpret_cast<const void*>(0xA2);
    CHECK(SteadyPan(flips,170.f,1000)==170.f);
    CHECK(SteadyPan(flips,-170.f,1016)==-170.f);         // 20 the short way: still possible
    CHECK(SteadyPan(flips,10.f,1032)==-170.f+25.f);      // 180 is not; it starts for the front
    CHECK(SteadyPan(flips,10.f,1048)==-120.f);           // and keeps walking there
    for(unsigned i=0;i<16;++i) SteadyPan(flips,10.f,1064+i*16);
    CHECK(SteadyPan(flips,10.f,1320)==0.f);              // arrives, and stays
    CHECK(SteadyPan(flips,-90.f,1336)==0.f);             // no longer placed at all
    CHECK(SteadyPan(flips,-90.f,1600)==-90.f);           // a gap is a new voice
    CHECK(SteadyPan(nullptr,-170.f,1616)==-170.f);       // no key, no state, no change
    g_soundPanStep=0;CHECK(SteadyPan(steady,170.f,1632)==170.f); // zero disables it wholly
    g_soundPanStep=25.f;g_soundPanFlip=0;
    const void* never=reinterpret_cast<const void*>(0xA3);
    CHECK(SteadyPan(never,170.f,2000)==170.f);
    CHECK(SteadyPan(never,-10.f,2016)==-10.f);           // nothing is ever fixed, so nothing moves
    CHECK(SteadyPan(never,-10.f,2032)==-10.f);
    g_soundPanFlip=120.f;
    // A short sound may step once on its way past without being taken for one
    // that cannot be placed. Four in a row is what the Ranger's gunfire is kept
    // out of the rule by.
    g_soundPanFlipCount=4;g_soundPanStep=25.f;
    const void* passing=reinterpret_cast<const void*>(0xA4);
    CHECK(SteadyPan(passing,0.f,3000)==0.f);
    CHECK(SteadyPan(passing,170.f,3016)==170.f);   // one step, still placed
    CHECK(SteadyPan(passing,0.f,3032)==0.f);       // two, still placed
    CHECK(SteadyPan(passing,170.f,3048)==170.f);   // three, still placed
    CHECK(SteadyPan(passing,0.f,3064)!=0.f);       // four: taken, and walking now
    g_soundPanFlipCount=4;g_soundPanStep=0;
    // Where each voice is put, which is the whole of the fix. The parameter
    // block the game handed in is never touched; a private copy carries this.
    g_soundPlaceVoices=true;g_soundFireWindowMs=120;g_soundCarriedMetres=2.f;
    g_soundCarriedAfterMs=150;g_soundCarriedMoveMetres=0.20f;
    g_soundUnderfootMetres=0.6f;g_soundFootDropMetres=1.7f;
    g_soundBackBehindMetres=0.35f;g_soundBackDownMetres=0.20f;
    g_lastFireAt=0;
    // Looking down +Z, so behind the head is -Z.
    for(unsigned k=0;k<3;++k) for(unsigned j=0;j<3;++j) g_headBasis[k][j]=k==j?1.f:0.f;
    g_headBasisAt=1000;
    SoundPositionSample v{};v.vr=v.fps=true;
    auto setPair=[&](float sx,float sy,float sz,float lx,float ly,float lz) {
        v.source[0]=sx;v.source[1]=sy;v.source[2]=sz;
        v.listener[0]=lx;v.listener[1]=ly;v.listener[2]=lz;
    };
    float at[3]{};
    // Something that happened at a place stays there while its emitter is
    // dragged about by the model's animation.
    v.key=reinterpret_cast<const void*>(0xB1);
    setPair(10,-1.8f,0, 10,0,0);                          // a splash, well below the ears
    CHECK(!PlaceVoice(v,1000,at));                        // nothing to move on the first sample
    CHECK(at[0]==10.f && at[1]==-1.8f);
    setPair(10.4f,-1.8f,0.3f, 10,0,0);
    CHECK(PlaceVoice(v,1016,at) && at[0]==10.f && at[2]==0.f);
    setPair(10.4f,-1.8f,0.3f, 13,0,0);                    // he walks off and it does not
    CHECK(PlaceVoice(v,1200,at) && at[0]==10.f);
    setPair(10.4f,-1.8f,0.3f, 20,0,0);
    CHECK(PlaceVoice(v,1216,at) && at[0]==10.f);
    // A voice beginning on the listener is his own, and is heard from the
    // ground beneath him wherever he goes.
    v.key=reinterpret_cast<const void*>(0xB2);
    setPair(30,0,0, 30,0,0);
    CHECK(PlaceVoice(v,2000,at));
    CHECK(at[0]==30.f && std::fabs(at[1]+1.7f)<.001f && at[2]==0.f);
    setPair(30.1f,0,0, 30.5f,0,0);
    CHECK(PlaceVoice(v,2016,at) && at[0]==30.5f && std::fabs(at[1]+1.7f)<.001f);
    // Once it proves to travel with him it moves to his back instead. A footfall
    // is over long before this and never does.
    setPair(33.1f,0,0, 33,0,0);g_headBasisAt=2190;
    CHECK(PlaceVoice(v,2200,at));
    CHECK(at[0]==33.f && std::fabs(at[1]+0.2f)<.001f && std::fabs(at[2]+0.35f)<.001f);
    setPair(40.2f,0,0, 40,0,0);g_headBasisAt=2210;
    CHECK(PlaceVoice(v,2216,at) && at[0]==40.f && std::fabs(at[2]+0.35f)<.001f);
    // A voice beginning just after a shot is that shot, and comes from the gun.
    g_lastFireMuzzle[0]=1.5f;g_lastFireMuzzle[1]=1.6f;g_lastFireMuzzle[2]=1.7f;
    g_lastFireAt=3000;
    v.key=reinterpret_cast<const void*>(0xB3);
    setPair(50,0,0, 50,0,0);
    CHECK(PlaceVoice(v,3050,at) && at[0]==1.5f && at[1]==1.6f && at[2]==1.7f);
    setPair(51,0,0, 52,0,0);
    CHECK(PlaceVoice(v,3066,at) && at[0]==1.5f);          // and stays at the gun
    v.key=reinterpret_cast<const void*>(0xB4);
    setPair(50,-1.8f,0, 50,0,0);
    CHECK(!PlaceVoice(v,3400,at) && at[1]==-1.8f);        // too late to be that shot
    v.key=reinterpret_cast<const void*>(0xB5);
    setPair(90,0,0, 50,0,0);
    CHECK(!PlaceVoice(v,3050,at) && at[0]==90.f);         // and too far to be it
    // A gap is a new voice, and the switch turns all of it off.
    g_lastFireAt=0;
    v.key=reinterpret_cast<const void*>(0xB1);
    setPair(70,-1.8f,0, 70,0,0);
    CHECK(!PlaceVoice(v,9000,at) && at[1]==-1.8f);
    g_soundPlaceVoices=false;
    setPair(71,-1.8f,0, 70,0,0);
    CHECK(!PlaceVoice(v,9016,at));
    g_soundPlaceVoices=true;
    v.vr=false;CHECK(!PlaceVoice(v,9032,at));             // VR off is never touched
    v.vr=true;v.fps=false;CHECK(!PlaceVoice(v,9048,at));  // nor third person
    v.fps=true;g_lastFireAt=0;g_headBasisAt=0;
    SoundPositionWindow w{};s.key=pos;s.pan=179;s.havePan=true;
    w.Add(s,100);s.pan=-179;w.Add(s,120);CHECK(w.panJump==2); // wrap is not a 358-degree jump
    w.Add(s,500);CHECK(w.samples==1 && w.panJump==0); // ignore gaps/new voices
    s.vr=!s.vr;w.Add(s,510);CHECK(w.samples==1); // separate VR OFF baseline
    g_soundSample=nullptr;
    // Execute EDF's actual 7A8A50 keep/remove decision; intercept its existing
    // CRI query rather than adding one. Synthetic manager supplies no engine.
    CHECK(InstallSoundLifecycleProbe());CHECK(!InstallSoundLifecycleProbe());
    g_soundPlaybackStatus=&PlaybackStatus;g_soundManagerUpdate=&ManagerUpdate;
    alignas(16) unsigned char voice[0x70]{};
    *reinterpret_cast<unsigned*>(voice+0x10)=123;
    *reinterpret_cast<float*>(voice+0x28)=1;
    *reinterpret_cast<float*>(voice+0x2c)=.5f;
    *reinterpret_cast<float*>(voice+0x38)=.75f;
    std::memcpy(voice+0x40,pos,sizeof(pos));voice[0x60]=1;
    unsigned char savedVoice[sizeof(voice)];std::memcpy(savedVoice,voice,sizeof(voice));
    lifeVoice=voice;lifeManager=manager;g_fencerProbeAt=GetTickCount64();g_soundViewMode=3;
    SoundLifeSample outerLife{};g_soundLifeSample=&outerLife;
    HookSoundManagerUpdate(manager,.016f);
    CHECK(statusCalls==1 && managerCalls==1 && g_soundLifeSample==&outerLife && !g_observedSoundManager);
    CHECK(g_soundLifeTotals.checks==1 && g_soundLifeTotals.accepted==1 && g_soundLifeTotals.status[2]==1);
    rawStatus=3;HookSoundManagerUpdate(manager,.016f);
    CHECK(statusCalls==2 && g_soundLifeTotals.removed==1 && g_soundLifeTotals.status[3]==1);
    CHECK(!std::memcmp(savedVoice,voice,sizeof(voice))); // keep/remove query is read-only
    rawStatus=2;g_soundViewMode=0;HookSoundManagerUpdate(manager,.016f); // F11+F6 baseline
    CHECK(g_soundLifeTotals.accepted==3 && g_soundLifeTotals.mode==0);
    // A busy diagnostics lock drops observation, never skips the real call.
    AcquireSRWLockExclusive(&g_soundLifeLock);
    HookSoundManagerUpdate(manager,.016f);
    ReleaseSRWLockExclusive(&g_soundLifeLock);
    CHECK(statusCalls==4 && g_soundLifeDropped>=1);
    g_fencerProbeAt=0;HookSoundManagerUpdate(manager,.016f);
    CHECK(statusCalls==5 && g_soundLifeTotals.accepted==3);
    CHECK(!std::memcmp(savedVoice,voice,sizeof(voice)));
    SoundLifeWindow life{};SoundLifeSample sample{};sample.voice=voice;sample.id=123;sample.status=2;sample.keep=true;
    life.Add(sample,1000);sample.id=124;sample.status=3;sample.keep=false;life.Add(sample,1020);
    CHECK(life.idChanges==1 && life.stateChanges==1 && life.removed==1);
    sample.mode=3;life.Add(sample,1040);CHECK(life.samples==1 && life.idChanges==0);
    g_soundLifeSample=nullptr;
    FreeLibrary(mapped);
    printf("Sound position native calculation / observing passthrough: %d failures\n",failures);
    return failures?1:0;
}
