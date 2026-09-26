#include "image_profile.h"
#include "first_person.h"
#include <cstdio>

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) { fprintf(stderr,"Usage: EDF6VRProbe.exe <absolute EDF.dll path>\n"); return 2; }
    // Map for structural inspection. Do not initialize or execute any EDF.dll routine.
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!module) { printf("Map failed: %lu\n",GetLastError()); return 1; }
    edf6vr::ImageProfile image{}; char reason[256]{};
    const bool good=edf6vr::CheckImage(module,image,reason,sizeof(reason));
    printf("%s\n",reason);
    if(good) printf("Image=%p slot RVA=%X original RVA=%X; NO PATCHES APPLIED\n",module,edf6vr::kUpdateSlotRva,edf6vr::kUpdateRva);
    const bool fps=good && edf6vr::CheckFirstPersonProfile(image);
    printf("FPS body vtable, draw entry, node lookup entry, Human body constructor: %s\n",fps?"PASS":"FAIL");
    FreeLibrary(module);
    return good&&fps?0:1;
}
