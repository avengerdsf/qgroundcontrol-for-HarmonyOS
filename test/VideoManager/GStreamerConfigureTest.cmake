cmake_minimum_required(VERSION 3.25)

get_filename_component(QGC_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(work "${QGC_SOURCE_DIR}/tmp/gst-v508-configure-test")
get_filename_component(_cmake_bin "${CMAKE_COMMAND}" DIRECTORY)
find_program(_test_ninja NAMES ninja HINTS "${_cmake_bin}" REQUIRED)
file(MAKE_DIRECTORY "${work}/source" "${work}/empty-sdk")
file(WRITE "${work}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(GStreamerConfigureTest LANGUAGES NONE)
set(QGC_PLATFORM_OHOS ON)
set(OHOS_ARCH x86_64)
list(PREPEND CMAKE_MODULE_PATH "${QGC_SOURCE_DIR}/cmake/find-modules")
if(DEFINED GSTREAMER_TEST_OTHER_FLAG)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/pkgconfig-fixture")
endif()
find_package(GStreamer REQUIRED COMPONENTS Core Base Video Gl GlPrototypes Rtsp GlEgl)
]=])

function(expect_rejected name expected)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -G Ninja -S "${work}/source" -B "${work}/${name}"
        "-DCMAKE_MAKE_PROGRAM=${_test_ninja}"
        "-DCMAKE_INSTALL_PREFIX=${work}/install"
        "-DQGC_SOURCE_DIR=${QGC_SOURCE_DIR}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "${expected}")
        message(FATAL_ERROR "${name}: expected rejection '${expected}', got ${result}:\n${output}\n${error}")
    endif()
    message(STATUS "PASS: ${name}")
endfunction()

expect_rejected(missing-sdk "OHOS requires GStreamer_ROOT_DIR"
    "-DGStreamer_ROOT_DIR=${work}/unavailable-sdk")
expect_rejected(dynamic-sdk "OHOS requires a static GStreamer SDK"
    "-DGStreamer_ROOT_DIR=${work}/empty-sdk" -DGStreamer_USE_STATIC_LIBS=OFF)
expect_rejected(incomplete-sdk "OHOS GStreamer SDK is incomplete"
    "-DGStreamer_ROOT_DIR=${work}/empty-sdk" -DGStreamer_USE_STATIC_LIBS=ON)

file(MAKE_DIRECTORY "${work}/manifest-sdk/lib/pkgconfig")
file(WRITE "${work}/manifest-sdk/lib/pkgconfig/gstreamer-1.0.pc" "")
file(WRITE "${work}/manifest-sdk/build-manifest.json"
    [[{"version":"1.22.12","abi":"x86_64","defaultLibrary":"static","glEnabled":true,"plugins":[]}]])
expect_rejected(wrong-version "OHOS GStreamer SDK version must be 1.28.4"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
file(WRITE "${work}/manifest-sdk/build-manifest.json"
    [[{"version":"1.28.4","abi":"arm64-v8a","defaultLibrary":"static","glEnabled":true,"plugins":[]}]])
expect_rejected(wrong-abi "does not match OHOS_ARCH"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
file(WRITE "${work}/manifest-sdk/build-manifest.json"
    [[{"version":"1.28.4","abi":"x86_64","defaultLibrary":"static","glEnabled":false,"plugins":[]}]])
expect_rejected(missing-gl "with GL/EGL enabled"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
file(WRITE "${work}/manifest-sdk/build-manifest.json"
    [[{"version":"1.28.4","abi":"x86_64","defaultLibrary":"static","glEnabled":true,"plugins":[]}]])
expect_rejected(missing-plugin "required plugin 'coreelements' is absent"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
set(_plugins coreelements app typefindfunctions playback videoconvertscale videotestsrc
    isomp4 matroska mpegtsdemux opengl openh264 libav vpx
    rtp rtpmanager rtsp sdpelem tcp udp videoparsersbad)
list(JOIN _plugins "\",\"" _plugins_json)
set(_manifest "{\"version\":\"1.28.4\",\"abi\":\"x86_64\",\"defaultLibrary\":\"static\",\"glEnabled\":true,\"plugins\":[\"${_plugins_json}\"]}")
file(WRITE "${work}/manifest-sdk/build-manifest.json" "${_manifest}")
expect_rejected(missing-tls "requires the static OpenSSL GIO backend"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
string(JSON _manifest SET "${_manifest}" gioModules [[ ["openssl"] ]])
string(JSON _manifest SET "${_manifest}" caBundle [[ "etc/ssl/certs/missing.crt" ]])
file(WRITE "${work}/manifest-sdk/build-manifest.json" "${_manifest}")
expect_rejected(missing-ca "requires its CA certificate bundle"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")
file(MAKE_DIRECTORY "${work}/manifest-sdk/etc/ssl/certs")
file(WRITE "${work}/manifest-sdk/etc/ssl/certs/missing.crt" "configuration fixture")
expect_rejected(missing-sysroot "requires a target CMAKE_SYSROOT"
    "-DGStreamer_ROOT_DIR=${work}/manifest-sdk")

# Feed malformed metadata into the real finder without requiring target binaries.
file(MAKE_DIRECTORY "${work}/source/pkgconfig-fixture" "${work}/sysroot")
file(WRITE "${work}/source/pkgconfig-fixture/FindPkgConfig.cmake" [=[
function(pkg_check_modules prefix)
    set(${prefix}_STATIC_LDFLAGS_OTHER "${GSTREAMER_TEST_OTHER_FLAG}" PARENT_SCOPE)
endfunction()
]=])
foreach(flag "/host/libgstreamer.a" "-Wl,-rpath,/host" "@host.rsp" "-Wl,--script,host.ld"
        "--sysroot=host" "-Wl,-lhost" "-Wl,--dynamic-list=host.syms")
    string(MAKE_C_IDENTIFIER "${flag}" name)
    expect_rejected("other-${name}" "unsupported OTHER flag"
        "-DGStreamer_ROOT_DIR=${work}/manifest-sdk" "-DCMAKE_SYSROOT=${work}/sysroot"
        "-DGSTREAMER_TEST_OTHER_FLAG=${flag}")
endforeach()
file(REMOVE_RECURSE "${work}")
