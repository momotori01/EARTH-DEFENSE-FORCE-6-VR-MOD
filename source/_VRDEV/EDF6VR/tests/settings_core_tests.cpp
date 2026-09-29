#include "settings/settings_core.h"
#include <cstdio>

using namespace edf6vr::settings;

static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

int main() {
    // The INI keeps every byte it is not asked to change.
    const std::string file="\xEF\xBB\xBF; header\r\n[LeftHanded]\r\n; which hand\r\nLeftHanded=0\r\nLeftHandedSticks=1\r\n\r\n"
                           "[Render]\r\nForceWidth=3840\r\nForceHeight=2160\n; odd line end kept\r\nReticleScale=0.5";
    auto ini=ParseIni(file);
    CHECK(WriteIni(ini)==file);
    CHECK(IniGet(ini,"lefthanded","LeftHanded")==std::optional<std::string>("0"));
    CHECK(IniGet(ini,"Render","reticlescale")==std::optional<std::string>("0.5"));
    CHECK(!IniGet(ini,"Render","Missing") && !IniGet(ini,"Nowhere","ForceWidth"));
    IniSet(ini,"LeftHanded","LeftHanded","1");
    IniSet(ini,"Render","ForceWidth","4800");
    std::string expected=file;
    expected.replace(expected.find("LeftHanded=0"),12,"LeftHanded=1");
    expected.replace(expected.find("ForceWidth=3840"),15,"ForceWidth=4800");
    CHECK(WriteIni(ini)==expected);
    // A missing key goes after the section's last key; the last line gets an end first.
    IniSet(ini,"Render","UiCluster","0");
    CHECK(WriteIni(ini)==expected+"\r\nUiCluster=0\r\n");
    IniSet(ini,"LeftHanded","Extra","2");
    CHECK(WriteIni(ini).find("LeftHandedSticks=1\r\nExtra=2\r\n\r\n[Render]")!=std::string::npos);
    // A missing section goes at the end, after a blank line.
    IniSet(ini,"VR","VehicleCockpit","0");
    CHECK(WriteIni(ini).size()>=26 && WriteIni(ini).substr(WriteIni(ini).size()-26)=="\r\n[VR]\r\nVehicleCockpit=0\r\n");
    CHECK(IniGet(ini,"VR","VehicleCockpit")==std::optional<std::string>("0"));
    // A file with LF lines gets LF lines.
    auto lf=ParseIni("[A]\nx=1\n");
    IniSet(lf,"A","y","2");
    CHECK(WriteIni(lf)=="[A]\nx=1\ny=2\n");
    // Comments that look like keys are not keys.
    auto commented=ParseIni("[A]\n; x=5\nx=1\n");
    CHECK(IniGet(commented,"A","x")==std::optional<std::string>("1"));

    // Versions.
    CHECK(ToString(ParseVersion("2.1.12"))=="2.1.12" && !ParseVersion("2.1").Valid() && !ParseVersion("2.1.2a").Valid());
    CHECK(ParseVersion("2.1.9")<ParseVersion("2.1.10") && !(ParseVersion("2.2.0")<ParseVersion("2.1.9")));
    CHECK(ToString(ManifestVersion("{\n  \"version\": \"2.1.2\",\n  \"packageRevision\": 2\n}"))=="2.1.2");
    CHECK(ToString(LatestReleaseVersion("{\"url\":\"x\",\"tag_name\":\"EDF6VR-2.1.3\",\"name\":\"EDF6VR-2.1.3\"}"))=="2.1.3");
    CHECK(!LatestReleaseVersion("{\"tag_name\":\"v2.1.3\"}").Valid());
    CHECK(ToString(DllVersion(std::string("xx EDF6VR 9 EDF6VR 2.1.2 cockpit loading, with")))=="2.1.2");
    CHECK(!DllVersion("EDF6VR 2.1.2 loading").Valid());

    // Picture size, as set_resolution.py.
    int w=0,h=0;
    CHECK(SizeForScale(1.0,w,h) && w==3840 && h==2160);
    CHECK(SizeForScale(0.8,w,h) && w==3072 && h==1728);
    CHECK(SizeForScale(1.25,w,h) && w==4800 && h==2700);
    CHECK(SizeForScale(2.0,w,h) && w==7680 && h==4320);
    CHECK(SizeForScale(0.5,w,h) && w==1920 && h==1080);
    CHECK(SizeForScale(1.1,w,h) && w==4224 && h==2376);
    CHECK(!SizeForScale(2.1,w,h) && !SizeForScale(0.4,w,h));
    double n=0;
    CHECK(ParseNumber(" 1,25 ",n) && n==1.25 && !ParseNumber("1.2x",n) && !ParseNumber("",n));

    // The builder's progress line.
    int percent=0; std::string detail;
    CHECK(ParseProgress("  [#########.............]  42%  upscale 120 of 300      about 20 min left",percent,detail));
    CHECK(percent==42 && detail=="upscale 120 of 300 about 20 min left");
    CHECK(ParseProgress("  [######################] 100%  finished",percent,detail) && percent==100 && detail=="finished");
    CHECK(!ParseProgress("[3 of 120]  MAP/ig_city.rab",percent,detail));

    std::printf("EDF6VR settings core tests: %d failures\n",failures);
    return failures?1:0;
}
