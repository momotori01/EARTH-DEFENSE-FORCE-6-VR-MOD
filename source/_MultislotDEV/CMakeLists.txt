cmake_minimum_required(VERSION 3.20)
project(EDF6MultiSlot VERSION 1.5.33 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# PluginAPI.h from EDFModLoader (BlueAmulet, MIT), next to the other projects.
set(MODLOADER_INCLUDE "${CMAKE_SOURCE_DIR}/../_VRDEV/EDFModLoader-src/EDFModLoader")
set(WARNINGS /W4 /WX /utf-8 /permissive-)

# The default settings the plugin writes when no INI exists come from EDF6MultiSlot.ini itself.
file(READ "${CMAKE_SOURCE_DIR}/EDF6MultiSlot.ini" MULTISLOT_DEFAULT_INI)
configure_file(src/default_ini.h.in "${CMAKE_BINARY_DIR}/generated/default_ini.h" @ONLY)
# The menu frame layout with the 8Player MOD label field (tools/make_menu_label.py); the plugin writes it
# to Mods/UI while it is active.
set(MENU_LAYOUT "${CMAKE_SOURCE_DIR}/assets/LYT_MAINFRAME.SGO")
file(READ "${MENU_LAYOUT}" MENU_LAYOUT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," MENU_LAYOUT_BYTES "${MENU_LAYOUT_HEX}")
set(BYTE "0x[0-9a-f][0-9a-f],")
string(REGEX REPLACE "(${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE}${BYTE})"
       "\\1\n" MENU_LAYOUT_BYTES "${MENU_LAYOUT_BYTES}")
configure_file(src/menu_layout.h.in "${CMAKE_BINARY_DIR}/generated/menu_layout.h" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/EDF6MultiSlot.ini" "${MENU_LAYOUT}")

# A build for helping with an investigation: diagnostic settings default on, so no INI editing is
# needed on a machine that is only lending a log. Off for everything shipped.
option(MULTISLOT_DIAGNOSTIC "Default the diagnostic settings to on" OFF)

add_library(EDF6MultiSlot SHARED
    src/plugin.cpp src/patches.cpp src/rooms.cpp src/roomview.cpp src/fakemembers.cpp src/hostmode.cpp src/menulayout.cpp
    src/mission.cpp src/spawn.cpp src/joinlog.cpp src/armor.cpp src/midhook.cpp src/code.cpp src/crashlog.cpp src/log.cpp
    src/netlog.cpp src/identity.cpp src/smoothing.cpp src/updatecheck.cpp src/traffic.cpp src/gaplog.cpp src/packetsize.cpp)

if(MULTISLOT_DIAGNOSTIC)
    target_compile_definitions(EDF6MultiSlot PRIVATE MULTISLOT_DIAGNOSTIC)
endif()
target_link_libraries(EDF6MultiSlot PRIVATE winhttp)
target_include_directories(EDF6MultiSlot PRIVATE ${MODLOADER_INCLUDE} "${CMAKE_BINARY_DIR}/generated")
target_compile_options(EDF6MultiSlot PRIVATE ${WARNINGS})
target_link_options(EDF6MultiSlot PRIVATE /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO /PDBALTPATH:EDF6MultiSlot.pdb)
set_target_properties(EDF6MultiSlot PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/dist/winmm.dll"
    "${CMAKE_BINARY_DIR}/loader-fix.json"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tools/fix_winmm_proxy.py"
        "${CMAKE_SOURCE_DIR}/third_party/EDFModLoader/winmm.dll"
        "${CMAKE_SOURCE_DIR}/dist/winmm.dll"
        --manifest "${CMAKE_BINARY_DIR}/loader-fix.json"
    DEPENDS tools/fix_winmm_proxy.py tools/pe.py third_party/EDFModLoader/winmm.dll
    VERBATIM)
add_custom_target(LoaderProxyFix ALL DEPENDS "${CMAKE_SOURCE_DIR}/dist/winmm.dll"
    "${CMAKE_BINARY_DIR}/loader-fix.json")
add_executable(LoaderProxyTests tests/loader_proxy_test.cpp)
add_dependencies(LoaderProxyTests LoaderProxyFix)
target_compile_options(LoaderProxyTests PRIVATE ${WARNINGS})
add_test(NAME LoaderProxyRace COMMAND LoaderProxyTests
    "${CMAKE_SOURCE_DIR}/third_party/EDFModLoader/winmm.dll" "${CMAKE_SOURCE_DIR}/dist/winmm.dll")
add_executable(HandshakeTests tests/handshake_test.cpp src/joinlog.cpp src/identity.cpp src/log.cpp)
target_compile_options(HandshakeTests PRIVATE ${WARNINGS})
add_test(NAME HandshakeUserSnapshots COMMAND HandshakeTests "${CMAKE_BINARY_DIR}/handshake-diagnostics-test.log")
add_executable(NetLogForwardTests tests/netlog_forward_test.cpp src/joinlog.cpp src/identity.cpp src/log.cpp
    src/traffic.cpp src/packetsize.cpp)
target_compile_options(NetLogForwardTests PRIVATE ${WARNINGS})
add_test(NAME NetworkImportForwarding COMMAND NetLogForwardTests)
set(EDF_DLL "${CMAKE_SOURCE_DIR}/../EDF.dll")

add_executable(MultiSlotTests tests/tests.cpp src/patches.cpp src/rooms.cpp src/mission.cpp src/midhook.cpp src/code.cpp src/log.cpp src/smoothing.cpp
    src/gaplog.cpp src/packetsize.cpp)
target_compile_options(MultiSlotTests PRIVATE ${WARNINGS})
add_test(NAME PatchTablesMatchEDF COMMAND MultiSlotTests "${EDF_DLL}")

add_executable(RoomViewTests tests/roomview_test.cpp src/roomview.cpp src/armor.cpp src/mission.cpp src/midhook.cpp
    src/code.cpp src/patches.cpp src/log.cpp src/identity.cpp src/joinlog.cpp)
target_compile_options(RoomViewTests PRIVATE ${WARNINGS})
add_test(NAME RoomViewPaging COMMAND RoomViewTests)

add_executable(MidHookTests tests/midhook_test.cpp src/midhook.cpp src/code.cpp)
target_compile_options(MidHookTests PRIVATE ${WARNINGS})
add_test(NAME MidFunctionHooks COMMAND MidHookTests)

add_executable(MissionTests tests/mission_test.cpp src/mission.cpp src/midhook.cpp src/code.cpp src/patches.cpp src/log.cpp)
target_compile_options(MissionTests PRIVATE ${WARNINGS})
add_test(NAME MissionSlots COMMAND MissionTests)

add_executable(HostModeTests tests/hostmode_test.cpp src/hostmode.cpp src/roomview.cpp src/mission.cpp src/armor.cpp
    src/midhook.cpp src/code.cpp src/patches.cpp src/log.cpp src/identity.cpp src/joinlog.cpp src/updatecheck.cpp)
target_link_libraries(HostModeTests PRIVATE winhttp)
target_compile_options(HostModeTests PRIVATE ${WARNINGS})
add_test(NAME HostModeAndMenuLabel COMMAND HostModeTests)

add_executable(MenuLayoutTests tests/menulayout_test.cpp src/menulayout.cpp)
target_include_directories(MenuLayoutTests PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_options(MenuLayoutTests PRIVATE ${WARNINGS})
add_test(NAME MenuLayoutFile COMMAND MenuLayoutTests "${CMAKE_BINARY_DIR}/layouttest")

add_executable(SpawnTests tests/spawn_test.cpp src/spawn.cpp src/mission.cpp src/midhook.cpp src/code.cpp src/patches.cpp src/log.cpp)
target_compile_options(SpawnTests PRIVATE ${WARNINGS})
add_test(NAME EnemySpawnCounts COMMAND SpawnTests)

add_executable(FakeMemberTests tests/fakemembers_test.cpp src/fakemembers.cpp src/roomview.cpp src/armor.cpp
    src/mission.cpp src/midhook.cpp src/code.cpp src/patches.cpp src/log.cpp src/identity.cpp src/joinlog.cpp)

add_executable(ArmorTests tests/armor_test.cpp src/armor.cpp src/mission.cpp src/midhook.cpp src/code.cpp
    src/patches.cpp src/log.cpp)
target_compile_options(ArmorTests PRIVATE ${WARNINGS})
add_test(NAME CopyArmor COMMAND ArmorTests)
target_compile_options(FakeMemberTests PRIVATE ${WARNINGS})
add_test(NAME FakeRoomMembers COMMAND FakeMemberTests)

add_executable(LogTests tests/log_test.cpp src/log.cpp)
target_compile_options(LogTests PRIVATE ${WARNINGS})
add_test(NAME LogFileCap COMMAND LogTests "${CMAKE_BINARY_DIR}/logtest")

add_executable(CrashDumpTests tests/crashdump_test.cpp src/crashlog.cpp src/log.cpp)
target_compile_options(CrashDumpTests PRIVATE ${WARNINGS})
add_test(NAME CrashDump COMMAND CrashDumpTests "${CMAKE_BINARY_DIR}/crashdumptest")

add_executable(TrafficTests tests/traffic_test.cpp src/traffic.cpp src/log.cpp)
target_compile_options(TrafficTests PRIVATE ${WARNINGS})
add_test(NAME TrafficMeter COMMAND TrafficTests "${CMAKE_BINARY_DIR}/traffictest")
add_executable(PacketSizeTests tests/packetsize_test.cpp src/packetsize.cpp src/log.cpp)
target_compile_options(PacketSizeTests PRIVATE ${WARNINGS})
add_test(NAME PacketSizeHandler COMMAND PacketSizeTests "${EDF_DLL}" "${CMAKE_BINARY_DIR}/packetsizetest")

add_executable(MultiSlotLoadTest tests/load_test.cpp src/patches.cpp src/midhook.cpp src/code.cpp
    src/gaplog.cpp src/packetsize.cpp src/log.cpp)
target_include_directories(MultiSlotLoadTest PRIVATE ${MODLOADER_INCLUDE} "${CMAKE_BINARY_DIR}/generated")
target_compile_options(MultiSlotLoadTest PRIVATE ${WARNINGS})
add_dependencies(MultiSlotLoadTest EDF6MultiSlot)
foreach(mode off host4 host8 fresh nomission recoveryoff quiet smooth reliable desync latch nolatch position facing sendall sendoff)
    add_test(NAME PluginLoad_${mode} COMMAND MultiSlotLoadTest "${EDF_DLL}" "$<TARGET_FILE:EDF6MultiSlot>"
             "${CMAKE_BINARY_DIR}/loadtest" ${mode})
endforeach()

configure_file(EDF6MultiSlot.ini "${CMAKE_SOURCE_DIR}/dist/EDF6MultiSlot.ini" COPYONLY)
