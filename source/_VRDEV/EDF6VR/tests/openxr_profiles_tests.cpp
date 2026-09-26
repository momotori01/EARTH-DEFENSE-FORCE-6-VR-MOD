// Exercise production action creation against strict Index-only/Touch-only
// runtimes. No installed runtime or hardware is loaded by this test.
#include "../src/openxr_session.cpp"
#include <map>
#include <vector>
using namespace edf6vr;
namespace {
std::map<std::string,XrPath> paths;
std::map<XrAction,std::string> actions;
std::map<std::string,std::string> accepted;
const char* selected=nullptr;
unsigned nextAction=0,profileReads=0;
int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);++failures;}}while(false)
XrResult XRAPI_PTR Path(XrInstance,const char* text,XrPath* out) {
    if(!paths.count(text)) paths[text]=paths.size()+1;
    *out=paths[text];return XR_SUCCESS;
}
std::string Name(XrPath path) {for(const auto& v:paths)if(v.second==path)return v.first;return {};}
XrResult XRAPI_PTR ActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out) {*out=reinterpret_cast<XrActionSet>(1);return XR_SUCCESS;}
XrResult XRAPI_PTR Action(XrActionSet,const XrActionCreateInfo* in,XrAction* out) {
    CHECK(in->countSubactionPaths==2);*out=reinterpret_cast<XrAction>(static_cast<uintptr_t>(++nextAction));
    actions[*out]=in->actionName;return XR_SUCCESS;
}
XrResult XRAPI_PTR Suggest(XrInstance,const XrInteractionProfileSuggestedBinding* in) {
    if(Name(in->interactionProfile)!=selected)return XR_ERROR_PATH_UNSUPPORTED;
    for(unsigned i=0;i<in->countSuggestedBindings;++i) {
        const auto& binding=in->suggestedBindings[i];const auto name=Name(binding.binding);
        CHECK(!name.empty());accepted[name]=actions[binding.action];
    }
    return XR_SUCCESS;
}
XrResult XRAPI_PTR Attach(XrSession,const XrSessionActionSetsAttachInfo* in) {CHECK(in->countActionSets==1);return XR_SUCCESS;}
XrResult XRAPI_PTR ActionSpace(XrSession,const XrActionSpaceCreateInfo* in,XrSpace* out) {
    CHECK(in->poseInActionSpace.orientation.w==1);*out=reinterpret_cast<XrSpace>(1);return XR_SUCCESS;
}
XrResult XRAPI_PTR Current(XrSession,XrPath,XrInteractionProfileState* out) {
    ++profileReads;return Path(g_instance,selected,&out->interactionProfile);
}
XrResult XRAPI_PTR Describe(XrInstance,XrPath path,uint32_t capacity,uint32_t* count,char* out) {
    auto name=Name(path);*count=static_cast<uint32_t>(name.size()+1);
    if(capacity<*count)return XR_ERROR_SIZE_INSUFFICIENT;
    std::memcpy(out,name.c_str(),*count);return XR_SUCCESS;
}
void XRAPI_PTR Unused() {CHECK(false);}
XrResult XRAPI_PTR Proc(XrInstance,const char* name,PFN_xrVoidFunction* out) {
#define API(n,f) if(!strcmp(name,n)){*out=reinterpret_cast<PFN_xrVoidFunction>(&f);return XR_SUCCESS;}
    API("xrStringToPath",Path) API("xrCreateActionSet",ActionSet) API("xrCreateAction",Action)
    API("xrSuggestInteractionProfileBindings",Suggest) API("xrAttachSessionActionSets",Attach)
    API("xrCreateActionSpace",ActionSpace) API("xrGetCurrentInteractionProfile",Current) API("xrPathToString",Describe)
#undef API
    *out=&Unused;return XR_SUCCESS; // required query APIs are not invoked during creation
}
}
int main() {
    g_runtimeModule=reinterpret_cast<HMODULE>(1);g_instance=reinterpret_cast<XrInstance>(1);g_session=reinterpret_cast<XrSession>(1);
    for(bool touch:{false,true}) {
        selected=touch?"/interaction_profiles/oculus/touch_controller":"/interaction_profiles/valve/index_controller";
        accepted.clear();g_inputReady=false;g_api.getProc=&Proc;
        CHECK(CreateInput());
        CHECK(accepted["/user/hand/right/input/trigger/value"]=="trigger");
        CHECK(accepted["/user/hand/left/input/thumbstick"]=="stick");
        CHECK(accepted["/user/hand/right/input/grip/pose"]=="grip_pose");
        CHECK(accepted["/user/hand/left/input/squeeze/value"]=="squeeze");
        CHECK(accepted[touch?"/user/hand/left/input/x/click":"/user/hand/left/input/a/click"]=="lower");
        CHECK(accepted[touch?"/user/hand/left/input/y/click":"/user/hand/left/input/b/click"]=="upper");
        CHECK(accepted.count("/user/hand/left/input/squeeze/force")==static_cast<size_t>(!touch));
        CHECK(accepted.count("/user/hand/left/input/menu/click")==static_cast<size_t>(touch));
    }
    CHECK(profileReads==4);
    // An explicit runtime path is selected without changing the machine registry.
    wchar_t old[MAX_PATH]{};const auto oldLength=GetEnvironmentVariableW(L"XR_RUNTIME_JSON",old,MAX_PATH);
    CHECK(SetEnvironmentVariableW(L"XR_RUNTIME_JSON",L"C:\\VR runtime\\test.json"));
    std::wstring manifest;CHECK(ActiveRuntimeManifest(manifest));CHECK(manifest==L"C:\\VR runtime\\test.json");
    const std::wstring longPath(MAX_PATH+1,L'x');CHECK(SetEnvironmentVariableW(L"XR_RUNTIME_JSON",longPath.c_str()));
    CHECK(!ActiveRuntimeManifest(manifest));
    SetEnvironmentVariableW(L"XR_RUNTIME_JSON",oldLength && oldLength<MAX_PATH?old:nullptr);
    g_runtimeModule=nullptr;g_instance=XR_NULL_HANDLE;g_session=XR_NULL_HANDLE;g_inputReady=false;g_api={};
    printf("Index / Touch automatic profile and runtime selection: %d failures (synthetic runtime, not hardware certification).\n",failures);
    return failures?1:0;
}
