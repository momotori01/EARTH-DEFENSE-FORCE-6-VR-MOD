// Exercise the production recorder, including frame association and worker flush.
#include "../src/motion_trace.cpp"
#include <thread>
#include <fstream>
#include <string>
using namespace edf6vr;
int main() {
    int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
    Matrix a{};for(unsigned i=0;i<4;++i) a.m[i][i]=1;a.m[3][0]=12;
    HmdSample h{};h.orientationValid=true;h.frame=1;h.displayTime=100;
    ConfigureMotionTrace(false,L"motion_trace_test",nullptr);
    TraceCamera(a,h,0,0);CHECK(cameraCount==0);
    ConfigureMotionTrace(true,L"motion_trace_test",nullptr);
    TraceFrameBegin(100,13);CHECK(state==1 && current.id==0); // menu does not start capture
    TraceCamera(a,h,.3f,0);TraceFrameBegin(100,13);CHECK(state==2 && current.beginCamera==1);
    float view[16]{};for(unsigned i=0;i<16;++i) view[i]=float(i)+.25f;
    TraceNativeMatrix(view,false,0);TraceNativeMatrix(view,true,7);
    std::thread other([&]{TraceNativeMatrix(view,false,0);});other.join();
    a.m[3][0]=24;h.frame=2;h.displayTime=200;TraceCamera(a,h,.4f,0);
    view[0]=999; // native outputs are copied, never retained as pointers
    XrCompositionLayerProjectionView submitted[2]{};XrView located[2]{};
    for(unsigned i=0;i<2;++i) {submitted[i].pose.orientation.w=1;submitted[i].pose.position.x=float(i);located[i].pose.position.x=float(i)+3;}
    TracePresent();TraceFrameEnd(submitted,2,h,XR_SUCCESS,located);
    CHECK(frameCount==1 && frames[0].beginCamera==1 && frames[0].endCamera==2);
    CHECK(frames[0].viewReads==1 && frames[0].projectionReads==1 && frames[0].handedness==7);
    CHECK(frames[0].view[0]==.25f && frames[0].projection[0]==.25f);
    CHECK(frames[0].latestSample==2 && frames[0].submitted[1].position.x==1 && frames[0].located[1].position.x==4);
    CHECK(cameras[0].matrix.m[3][0]==12 && cameras[1].matrix.m[3][0]==24);
    CHECK(frames[0].present>=frames[0].begin && frames[0].end>=frames[0].present);
    // Reporting cannot stall a rendering callback: busy lock drops a sample.
    AcquireSRWLockExclusive(&lock);TraceNativeMatrix(view,false,0);ReleaseSRWLockExclusive(&lock);CHECK(drops==1);
    TraceFrameBegin(200,13);TraceFrameEnd(nullptr,0,h,XR_ERROR_RUNTIME_FAILURE);
    CHECK(frameCount==2 && frames[1].viewReads==0 && frames[1].count==0 && frames[1].result==XR_ERROR_RUNTIME_FAILURE);
    FinishMotionTrace();CHECK(state==3);FlushMotionTrace();CHECK(state==5);
    TraceCamera(a,h,0,0);CHECK(cameraCount==2); // completed buffer immutable
    unsigned files=0;
    for(const auto& entry:std::filesystem::directory_iterator(L"motion_trace_test")) {
        if(entry.path().extension()!=L".csv") continue;
        std::ifstream in(entry.path());std::string line;std::getline(in,line);CHECK(line.find("# frequency=")==0);
        std::getline(in,line);CHECK(line.find("qpc,id,")==0 || line.find("id,begin,")==0);++files;
    }
    CHECK(files>=2);
    ConfigureMotionTrace(true,L"motion_trace_test",nullptr);TraceCamera(a,h,0,0);TraceFrameBegin(300,13);
    first-=frequency*31;TraceFrameEnd(submitted,2,h,XR_SUCCESS);CHECK(state==3); // 30s bound without sleeping
    ConfigureMotionTrace(false,nullptr,nullptr);
    printf("Motion trace failures=%d\n",failures);return failures?1:0;
}
