if(QGC_PLATFORM_OHOS OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    if(NOT GStreamer_ROOT_DIR)
        if(GSTREAMER_ROOT)
            set(GStreamer_ROOT_DIR "${GSTREAMER_ROOT}")
        elseif(GStreamer_ROOT)
            set(GStreamer_ROOT_DIR "${GStreamer_ROOT}")
        endif()
    endif()
    if(NOT GStreamer_ROOT_DIR OR NOT IS_DIRECTORY "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "OHOS requires GStreamer_ROOT_DIR pointing to a target SDK")
    endif()
    if(DEFINED GStreamer_USE_STATIC_LIBS AND NOT GStreamer_USE_STATIC_LIBS)
        message(FATAL_ERROR "OHOS requires a static GStreamer SDK")
    endif()
    set(GStreamer_USE_STATIC_LIBS ON)
    set(GStreamer_USE_FRAMEWORK OFF)
    set(GStreamer_FIND_VERSION 1.28.4)
    cmake_path(ABSOLUTE_PATH GStreamer_ROOT_DIR NORMALIZE)
    set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib")
    set(GSTREAMER_PLUGIN_PATH "${GSTREAMER_LIB_PATH}/gstreamer-1.0")
    set(GSTREAMER_INCLUDE_PATH "${GStreamer_ROOT_DIR}/include")
    if(NOT EXISTS "${GStreamer_ROOT_DIR}/build-manifest.json"
            OR NOT EXISTS "${GSTREAMER_LIB_PATH}/pkgconfig/gstreamer-1.0.pc")
        message(FATAL_ERROR "OHOS GStreamer SDK is incomplete: build-manifest.json and target pkg-config files are required")
    endif()
    file(READ "${GStreamer_ROOT_DIR}/build-manifest.json" _gst_manifest)
    foreach(field version abi defaultLibrary glEnabled plugins)
        string(JSON _gst_${field} ERROR_VARIABLE _gst_manifest_error GET "${_gst_manifest}" "${field}")
        if(_gst_manifest_error)
            message(FATAL_ERROR "OHOS GStreamer SDK manifest is missing '${field}': ${_gst_manifest_error}")
        endif()
    endforeach()
    if(NOT _gst_version VERSION_EQUAL 1.28.4)
        message(FATAL_ERROR "OHOS GStreamer SDK version must be 1.28.4, got ${_gst_version}")
    endif()
    if(NOT _gst_abi STREQUAL OHOS_ARCH)
        message(FATAL_ERROR "OHOS GStreamer SDK ABI ${_gst_abi} does not match OHOS_ARCH ${OHOS_ARCH}")
    endif()
    if(NOT _gst_defaultLibrary STREQUAL "static" OR NOT _gst_glEnabled)
        message(FATAL_ERROR "OHOS requires a static GStreamer SDK with GL/EGL enabled")
    endif()
    string(JSON _gst_plugin_count LENGTH "${_gst_manifest}" plugins)
    math(EXPR _gst_plugin_last "${_gst_plugin_count} - 1")
    set(GSTREAMER_PLUGINS)
    if(_gst_plugin_count GREATER 0)
        foreach(index RANGE ${_gst_plugin_last})
            string(JSON plugin GET "${_gst_manifest}" plugins ${index})
            if(NOT plugin MATCHES "^[a-zA-Z0-9_]+$")
                message(FATAL_ERROR "Invalid OHOS GStreamer plugin name: ${plugin}")
            endif()
            list(APPEND GSTREAMER_PLUGINS "${plugin}")
        endforeach()
    endif()
    foreach(plugin coreelements app typefindfunctions playback videoconvertscale videotestsrc
            isomp4 matroska mpegtsdemux opengl openh264 libav vpx
            rtp rtpmanager rtsp sdpelem tcp udp videoparsersbad)
        if(NOT plugin IN_LIST GSTREAMER_PLUGINS)
            message(FATAL_ERROR "OHOS GStreamer SDK is incomplete: required plugin '${plugin}' is absent")
        endif()
    endforeach()
    string(JSON _gst_gio_modules ERROR_VARIABLE _gst_manifest_error GET "${_gst_manifest}" gioModules)
    if(_gst_manifest_error OR NOT _gst_gio_modules MATCHES "\"openssl\"")
        message(FATAL_ERROR "OHOS GStreamer SDK requires the static OpenSSL GIO backend")
    endif()
    string(JSON GSTREAMER_CA_BUNDLE ERROR_VARIABLE _gst_manifest_error GET "${_gst_manifest}" caBundle)
    if(_gst_manifest_error OR IS_ABSOLUTE "${GSTREAMER_CA_BUNDLE}" OR GSTREAMER_CA_BUNDLE MATCHES "(^|/)\\.\\.(/|$)"
            OR NOT EXISTS "${GStreamer_ROOT_DIR}/${GSTREAMER_CA_BUNDLE}")
        message(FATAL_ERROR "OHOS GStreamer SDK requires its CA certificate bundle")
    endif()
    if(NOT CMAKE_SYSROOT OR NOT IS_DIRECTORY "${CMAKE_SYSROOT}")
        message(FATAL_ERROR "OHOS GStreamer requires a target CMAKE_SYSROOT")
    endif()

    # Query only the target prefix, even when the parser itself is a host tool.
    foreach(name PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR)
        set(_gst_saved_${name} "$ENV{${name}}")
    endforeach()
    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
    if(CMAKE_HOST_WIN32)
        set(_gst_pc_separator ";")
    else()
        set(_gst_pc_separator ":")
    endif()
    set(ENV{PKG_CONFIG_LIBDIR} "${GSTREAMER_LIB_PATH}/pkgconfig${_gst_pc_separator}${GSTREAMER_PLUGIN_PATH}/pkgconfig")
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH FALSE)
    set(PKG_CONFIG_ARGN --static --dont-define-prefix
        "--define-variable=prefix=${GStreamer_ROOT_DIR}"
        "--define-variable=libdir=${GSTREAMER_LIB_PATH}"
        "--define-variable=includedir=${GSTREAMER_INCLUDE_PATH}")
    find_package(PkgConfig REQUIRED)
    set(_gst_modules gstreamer-1.0=1.28.4 gstreamer-base-1.0=1.28.4 gstreamer-video-1.0=1.28.4
        gstreamer-gl-1.0=1.28.4 gstreamer-gl-prototypes-1.0=1.28.4 gstreamer-gl-egl-1.0=1.28.4
        gstreamer-rtsp-1.0=1.28.4 gstreamer-app-1.0=1.28.4 gioopenssl)
    foreach(plugin IN LISTS GSTREAMER_PLUGINS)
        list(APPEND _gst_modules "gst${plugin}=1.28.4")
    endforeach()
    if(QGC_BUILD_TESTING)
        foreach(plugin x264 x265 mpegtsmux)
            if(NOT plugin IN_LIST GSTREAMER_PLUGINS)
                message(FATAL_ERROR "OHOS GStreamer tests require plugin '${plugin}'")
            endif()
        endforeach()
    endif()
    if(QGC_BUILD_TESTING OR RtspServer IN_LIST GStreamer_FIND_COMPONENTS)
        list(APPEND _gst_modules gstreamer-rtsp-server-1.0=1.28.4)
        set(GStreamer_RtspServer_FOUND TRUE)
    endif()
    pkg_check_modules(PC_GST_OHOS REQUIRED ${_gst_modules})
    foreach(name PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR)
        set(ENV{${name}} "${_gst_saved_${name}}")
    endforeach()
    # The target SDK needs only static defines, pthread and GModule exports.
    # Every include/library path must use the separately validated lists.
    foreach(flag IN LISTS PC_GST_OHOS_STATIC_CFLAGS_OTHER)
        if(NOT flag STREQUAL "-pthread" AND NOT flag MATCHES "^-D[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$")
            message(FATAL_ERROR "OHOS GStreamer pkg-config contains an unsupported OTHER flag: ${flag}")
        endif()
    endforeach()
    foreach(flag IN LISTS PC_GST_OHOS_STATIC_LDFLAGS_OTHER)
        if(NOT flag STREQUAL "-pthread" AND NOT flag STREQUAL "-Wl,--export-dynamic")
            message(FATAL_ERROR "OHOS GStreamer pkg-config contains an unsupported OTHER flag: ${flag}")
        endif()
    endforeach()

    set(_gst_static_libraries)
    set(_gst_saved_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    foreach(library IN LISTS PC_GST_OHOS_STATIC_LIBRARIES)
        unset(_gst_library CACHE)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
        find_library(_gst_library NAMES "${library}"
            PATHS "${GSTREAMER_LIB_PATH}" "${GSTREAMER_PLUGIN_PATH}" "${GSTREAMER_LIB_PATH}/gio/modules"
            NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
        if(_gst_library)
            list(APPEND _gst_static_libraries "${_gst_library}")
        elseif(library MATCHES "^(c|m|dl|pthread|rt)$")
            list(APPEND _gst_static_libraries "${library}")
        elseif(library MATCHES "^(c\\+\\+|unwind|atomic)$")
            # The C++ runtime ships with Qt (libc++_shared.so) and the OHOS toolchain links it
            # implicitly; the sysroot has no static archive for these, so skip them like the
            # desktop/Android backends do.
        else()
            # Platform libraries may be shared, but must come from the target sysroot.
            set(CMAKE_FIND_LIBRARY_SUFFIXES .so .a)
            find_library(_gst_library NAMES "${library}"
                PATHS "${CMAKE_SYSROOT}/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
                    "${CMAKE_SYSROOT}/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-ohos"
                    "${CMAKE_SYSROOT}/usr/lib"
                NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
            if(NOT _gst_library)
                message(FATAL_ERROR "OHOS GStreamer static dependency '${library}' is missing from the target SDK/sysroot")
            endif()
            list(APPEND _gst_static_libraries "${_gst_library}")
        endif()
    endforeach()
    unset(_gst_library CACHE)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_gst_saved_library_suffixes}")
    foreach(path IN LISTS PC_GST_OHOS_STATIC_INCLUDE_DIRS PC_GST_OHOS_STATIC_LIBRARY_DIRS)
        cmake_path(IS_PREFIX GStreamer_ROOT_DIR "${path}" NORMALIZE _gst_in_prefix)
        if(NOT _gst_in_prefix)
            message(FATAL_ERROR "OHOS GStreamer pkg-config leaked a path outside the target prefix: ${path}")
        endif()
    endforeach()
    add_library(GStreamer::GStreamer INTERFACE IMPORTED)
    target_include_directories(GStreamer::GStreamer INTERFACE ${PC_GST_OHOS_STATIC_INCLUDE_DIRS})
    target_compile_options(GStreamer::GStreamer INTERFACE ${PC_GST_OHOS_STATIC_CFLAGS_OTHER})
    target_link_options(GStreamer::GStreamer INTERFACE ${PC_GST_OHOS_STATIC_LDFLAGS_OTHER})
    target_link_libraries(GStreamer::GStreamer INTERFACE
        "-Wl,--start-group" ${_gst_static_libraries} "-Wl,--end-group")
    target_compile_definitions(GStreamer::GStreamer INTERFACE QGC_GST_STATIC_BUILD)
    foreach(plugin IN LISTS GSTREAMER_PLUGINS)
        target_compile_definitions(GStreamer::GStreamer INTERFACE "GST_PLUGIN_${plugin}_FOUND")
    endforeach()
    set(_gst_components Core Base Video Gl GlPrototypes GlEgl Rtsp App)
    if(GStreamer_RtspServer_FOUND)
        list(APPEND _gst_components RtspServer)
    endif()
    foreach(component IN LISTS _gst_components)
        set(GStreamer_${component}_FOUND TRUE)
        add_library(GStreamer::${component} INTERFACE IMPORTED)
        target_link_libraries(GStreamer::${component} INTERFACE GStreamer::GStreamer)
    endforeach()
    set(GStreamer_VERSION 1.28.4)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(GStreamer VERSION_VAR GStreamer_VERSION HANDLE_COMPONENTS)
    return()
endif()

if(NOT DEFINED GStreamer_FIND_VERSION)
    if(LINUX)
        set(GStreamer_FIND_VERSION 1.20)
    else()
        set(GStreamer_FIND_VERSION 1.22.12)
    endif()
endif()

if(NOT DEFINED GStreamer_ROOT_DIR)
    if(DEFINED GSTREAMER_ROOT)
        set(GStreamer_ROOT_DIR ${GSTREAMER_ROOT})
    elseif(DEFINED GStreamer_ROOT)
        set(GStreamer_ROOT_DIR ${GStreamer_ROOT})
    endif()

    if(DEFINED GStreamer_ROOT_DIR AND NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(STATUS "The user provided GStreamer directory does not exist: ${GStreamer_ROOT_DIR}")
    endif()
endif()

if(NOT DEFINED GStreamer_USE_STATIC_LIBS)
    if(ANDROID OR IOS)
        set(GStreamer_USE_STATIC_LIBS ON)
    else()
        set(GStreamer_USE_STATIC_LIBS OFF)
    endif()
endif()

if(NOT DEFINED GStreamer_USE_FRAMEWORK)
    if(APPLE)
        set(GStreamer_USE_FRAMEWORK ON)
    else()
        set(GStreamer_USE_FRAMEWORK OFF)
    endif()
endif()

################################################################################

set(PKG_CONFIG_ARGN)

if(WIN32)
    if(NOT DEFINED GStreamer_ROOT_DIR)
        if(DEFINED ENV{GSTREAMER_1_0_ROOT_X86_64} AND EXISTS "$ENV{GSTREAMER_1_0_ROOT_X86_64}")
            set(GStreamer_ROOT_DIR "$ENV{GSTREAMER_1_0_ROOT_X86_64}")
        elseif(MSVC AND DEFINED ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64} AND EXISTS "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}")
            set(GStreamer_ROOT_DIR "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}")
        elseif(MINGW AND DEFINED ENV{GSTREAMER_1_0_ROOT_MINGW_X86_64} AND EXISTS "$ENV{GSTREAMER_1_0_ROOT_MINGW_X86_64}")
            set(GStreamer_ROOT_DIR "$ENV{GSTREAMER_1_0_ROOT_MINGW_X86_64}")
        elseif(EXISTS "C:/Program Files/gstreamer/1.0/msvc_x86_64")
            set(GStreamer_ROOT_DIR "C:/Program Files/gstreamer/1.0/msvc_x86_64")
        elseif(EXISTS "C:/gstreamer/1.0/msvc_x86_64")
            set(GStreamer_ROOT_DIR "C:/gstreamer/1.0/msvc_x86_64")
        endif()
    endif()

    cmake_path(CONVERT "${GStreamer_ROOT_DIR}" TO_CMAKE_PATH_LIST GStreamer_ROOT_DIR NORMALIZE)
    if(NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib")
    set(GSTREAMER_PLUGIN_PATH "${GSTREAMER_LIB_PATH}/gstreamer-1.0")
    set(GSTREAMER_INCLUDE_PATH "${GStreamer_ROOT_DIR}/include")

    set(ENV{PKG_CONFIG} "${GStreamer_ROOT_DIR}/bin")
    set(PKG_CONFIG_EXECUTABLE "$ENV{PKG_CONFIG}/pkg-config.exe")
    set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_LIB_PATH}/pkgconfig;${GSTREAMER_PLUGIN_PATH}/pkgconfig;$ENV{PKG_CONFIG_PATH}")
    list(APPEND PKG_CONFIG_ARGN
        --dont-define-prefix
        --define-variable=prefix=${GStreamer_ROOT_DIR}
        --define-variable=libdir=${GSTREAMER_LIB_PATH}
        --define-variable=includedir=${GSTREAMER_INCLUDE_PATH}
    )
elseif(LINUX)
    if(NOT DEFINED GStreamer_ROOT_DIR)
        if(EXISTS "/usr")
            set(GStreamer_ROOT_DIR "/usr")
        endif()
    endif()

    cmake_path(CONVERT "${GStreamer_ROOT_DIR}" TO_CMAKE_PATH_LIST GStreamer_ROOT_DIR NORMALIZE)
    if(NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    if((EXISTS "${GStreamer_ROOT_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu" ) AND (EXISTS "${GStreamer_ROOT_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/gstreamer-1.0"))
        set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu")
    elseif(EXISTS "${GStreamer_ROOT_DIR}/lib")
        set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib")
    else()
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    set(GSTREAMER_PLUGIN_PATH "${GSTREAMER_LIB_PATH}/gstreamer-1.0")
    set(GSTREAMER_INCLUDE_PATH "${GStreamer_ROOT_DIR}/include")

    set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_LIB_PATH}/pkgconfig:$ENV{PKG_CONFIG_PATH}")
elseif(ANDROID)
    # https://gstreamer.freedesktop.org/data/pkg/android/${GStreamer_FIND_VERSION}/gstreamer-1.0-android-universal-${GStreamer_FIND_VERSION}.tar.xz.sha256sum
    CPMAddPackage(
        NAME gstreamer
        VERSION ${GStreamer_FIND_VERSION}
        URL "https://gstreamer.freedesktop.org/data/pkg/android/${GStreamer_FIND_VERSION}/gstreamer-1.0-android-universal-${GStreamer_FIND_VERSION}.tar.xz"
        # URL_HASH be92cf477d140c270b480bd8ba0e26b1e01c8db042c46b9e234d87352112e485
    )

    if(NOT DEFINED GStreamer_ROOT_DIR)
        if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
            set(GStreamer_ROOT_DIR "${gstreamer_SOURCE_DIR}/armv7")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
            set(GStreamer_ROOT_DIR "${gstreamer_SOURCE_DIR}/arm64")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
            set(GStreamer_ROOT_DIR "${gstreamer_SOURCE_DIR}/x86")
        elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
            set(GStreamer_ROOT_DIR "${gstreamer_SOURCE_DIR}/x86_64")
        endif()
    endif()

    cmake_path(CONVERT "${GStreamer_ROOT_DIR}" TO_CMAKE_PATH_LIST GStreamer_ROOT_DIR NORMALIZE)
    if(NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib")
    set(GSTREAMER_PLUGIN_PATH "${GSTREAMER_LIB_PATH}/gstreamer-1.0")
    set(GSTREAMER_INCLUDE_PATH "${GStreamer_ROOT_DIR}/include")

    set(ENV{PKG_CONFIG_PATH} "")
    if(CMAKE_HOST_WIN32)
        set(ENV{PKG_CONFIG} "${GStreamer_ROOT_DIR}/share/gst-android/ndk-build/tools/windows")
        set(PKG_CONFIG_EXECUTABLE "$ENV{PKG_CONFIG}/pkg-config.exe")
        set(ENV{PKG_CONFIG_LIBDIR} "${GSTREAMER_LIB_PATH}/pkgconfig;${GSTREAMER_PLUGIN_PATH}/pkgconfig")
        list(APPEND PKG_CONFIG_ARGN --dont-define-prefix)
    elseif(CMAKE_HOST_UNIX)
        set(ENV{PKG_CONFIG_LIBDIR} "${GSTREAMER_LIB_PATH}/pkgconfig:${GSTREAMER_PLUGIN_PATH}/pkgconfig")
    endif()
    list(APPEND PKG_CONFIG_ARGN
        --define-variable=prefix=${GStreamer_ROOT_DIR}
        --define-variable=libdir=${GSTREAMER_LIB_PATH}
        --define-variable=includedir=${GSTREAMER_INCLUDE_PATH}
    )
elseif(MACOS)
    if(NOT DEFINED GStreamer_ROOT_DIR)
        if(EXISTS "/Library/Frameworks/GStreamer.framework")
            set(GStreamer_ROOT_DIR "/Library/Frameworks/GStreamer.framework/Versions/1.0")
        elseif(EXISTS "/opt/homebrew/opt/gstreamer")
            set(GStreamer_ROOT_DIR "/opt/homebrew/opt/gstreamer")
        elseif(EXISTS "/usr/local/opt/gstreamer")
            set(GStreamer_ROOT_DIR "/usr/local/opt/gstreamer")
        endif()
    endif()

    cmake_path(CONVERT "${GStreamer_ROOT_DIR}" TO_CMAKE_PATH_LIST GStreamer_ROOT_DIR NORMALIZE)
    if(NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    if(GStreamer_USE_FRAMEWORK)
        set(GSTREAMER_FRAMEWORK_PATH "${GStreamer_ROOT_DIR}/../..")
    endif()

    set(GSTREAMER_INCLUDE_PATH "${GStreamer_ROOT_DIR}/include")
    set(GSTREAMER_LIB_PATH "${GStreamer_ROOT_DIR}/lib")
    set(GSTREAMER_PLUGIN_PATH "${GSTREAMER_LIB_PATH}/gstreamer-1.0")

    set(ENV{PKG_CONFIG} "${GStreamer_ROOT_DIR}/bin")
    set(PKG_CONFIG_EXECUTABLE "$ENV{PKG_CONFIG}/pkg-config")
    set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_LIB_PATH}/pkgconfig:${GSTREAMER_PLUGIN_PATH}/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    list(APPEND PKG_CONFIG_ARGN
        --dont-define-prefix
        --define-variable=prefix=${GStreamer_ROOT_DIR}
        --define-variable=libdir=${GSTREAMER_LIB_PATH}
        --define-variable=includedir=${GSTREAMER_INCLUDE_PATH}
    )
elseif(IOS)
    message(FATAL_ERROR "GStreamer for iOS is Currently Unsupported.")

    CPMAddPackage(
        NAME gstreamer
        VERSION ${GStreamer_FIND_VERSION}
        URL "https://gstreamer.freedesktop.org/data/pkg/ios/${GStreamer_FIND_VERSION}/gstreamer-1.0-devel-${GStreamer_FIND_VERSION}-ios-universal.pkg"
    )

    set(GST_PKG_FILE "${gstreamer_SOURCE_DIR}/gstreamer.pkg")
    set(GST_EXPAND_DIR "${gstreamer_SOURCE_DIR}/gstreamer-pkg-expanded")
    set(GST_PAYLOAD_DIR "${GST_EXPAND_DIR}/Payload")

    file(MAKE_DIRECTORY "${GST_EXPAND_DIR}")
    execute_process(
        COMMAND pkgutil --expand-full "${GST_PKG_FILE}" "${GST_EXPAND_DIR}"
        RESULT_VARIABLE _pkgutil_rc
    )
    if(NOT _pkgutil_rc EQUAL 0)
        message(FATAL_ERROR "pkgutil failed to expand GStreamer .pkg")
    endif()

    execute_process(
        COMMAND xar -xf "${GST_EXPAND_DIR}/gstreamer-1.0-devel-${GStreamer_FIND_VERSION}-ios-universal.pkg/Payload"
                --directory "${GST_PAYLOAD_DIR}"
        RESULT_VARIABLE _xar_rc
    )
    if(NOT _xar_rc EQUAL 0)
        message(FATAL_ERROR "xar failed to extract GStreamer Payload")
    endif()

    # set(GSTREAMER_FRAMEWORK_PATH "/Library/Developer/GStreamer/iPhone.sdk" CACHE PATH "Path of GStreamer.Framework")
    set(GSTREAMER_FRAMEWORK_PATH "${GST_PAYLOAD_DIR}/usr/local/Frameworks/GStreamer.framework")

    set(GStreamer_ROOT_DIR "${GSTREAMER_FRAMEWORK_PATH}/Versions/1.0")

    if(NOT EXISTS "${GStreamer_ROOT_DIR}")
        message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
    endif()

    set(GSTREAMER_INCLUDE_PATH "${GSTREAMER_FRAMEWORK_PATH}/Headers")
endif()

if(NOT EXISTS "${GStreamer_ROOT_DIR}" OR NOT EXISTS "${GSTREAMER_LIB_PATH}" OR NOT EXISTS "${GSTREAMER_PLUGIN_PATH}" OR NOT EXISTS "${GSTREAMER_INCLUDE_PATH}")
    message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
endif()

if(GStreamer_USE_FRAMEWORK AND NOT EXISTS "${GSTREAMER_FRAMEWORK_PATH}")
    message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
endif()

################################################################################

if(GStreamer_USE_STATIC_LIBS)
    set(GSTREAMER_EXTRA_DEPS
        gstreamer-base-1.0
        gstreamer-video-1.0
        gstreamer-gl-1.0
        gstreamer-gl-prototypes-1.0
        gstreamer-rtsp-1.0
        # gstreamer-gl-egl-1.0
        # gstreamer-gl-wayland-1.0
        # gstreamer-gl-x11-1.0
    )

    set(GSTREAMER_PLUGINS
        coreelements
        dav1d
        isomp4
        libav
        matroska
        mpegtsdemux
        opengl
        openh264
        playback
        rtp
        rtpmanager
        rtsp
        sdpelem
        tcp
        typefindfunctions
        udp
        videoparsersbad
        vpx
    )
    if(ANDROID)
        list(APPEND GSTREAMER_PLUGINS androidmedia) # vulkan
    elseif(APPLE)
        list(APPEND GSTREAMER_PLUGINS applemedia vulkan)
    elseif(WIN32)
        list(APPEND GSTREAMER_PLUGINS d3d d3d11 d3d12 dxva nvcodec)
    elseif(LINUX)
        list(APPEND GSTREAMER_PLUGINS nvcodec qsv va vulkan) # qml6 - GStreamer provided qml6 is xcb only
    endif()
endif()

if(ANDROID)
    set(GStreamer_Mobile_MODULE_NAME gstreamer_android)
    set(G_IO_MODULES openssl)
    set(G_IO_MODULES_PATH "${GStreamer_ROOT_DIR}/lib/gio/modules")

    set(GStreamer_NDK_BUILD_PATH  "${GStreamer_ROOT_DIR}/share/gst-android/ndk-build/")
    set(GSTREAMER_ANDROID_MODULE_NAME gstreamer_android)
    set(GSTREAMER_JAVA_SRC_DIR "${CMAKE_BINARY_DIR}/android-build-${CMAKE_PROJECT_NAME}/src")
    set(GSTREAMER_ASSETS_DIR "${CMAKE_BINARY_DIR}/android-build-${CMAKE_PROJECT_NAME}/assets")

    configure_file(
        "${GStreamer_NDK_BUILD_PATH}/gstreamer_android-1.0.c.in"
        "${GStreamer_Mobile_MODULE_NAME}.c"
    )
endif()

################################################################################

if(GStreamer_USE_FRAMEWORK)
    list(APPEND CMAKE_FRAMEWORK_PATH "${GSTREAMER_FRAMEWORK_PATH}")
endif()

if(GStreamer_USE_STATIC_LIBS)
    list(APPEND PKG_CONFIG_ARGN "--static")
endif()

find_package(PkgConfig REQUIRED QUIET)

list(PREPEND CMAKE_PREFIX_PATH ${GStreamer_ROOT_DIR})
if(LINUX)
    pkg_check_modules(PC_GSTREAMER REQUIRED gstreamer-1.0>=${GStreamer_FIND_VERSION})
else()
    pkg_check_modules(PC_GSTREAMER REQUIRED gstreamer-1.0=${GStreamer_FIND_VERSION})
endif()
set(GStreamer_VERSION "${PC_GSTREAMER_VERSION}")

################################################################################

function(find_gstreamer_component component pkgconfig_name)
    set(target GStreamer::${component})

    if(NOT TARGET ${target})
        string(TOUPPER ${component} upper)
        pkg_check_modules(PC_GSTREAMER_${upper} IMPORTED_TARGET ${pkgconfig_name})
        if(TARGET PkgConfig::PC_GSTREAMER_${upper})
            qt_add_library(GStreamer::${component} INTERFACE IMPORTED)
            target_link_libraries(GStreamer::${component} INTERFACE PkgConfig::PC_GSTREAMER_${upper})
            if("PC_GSTREAMER_${upper}" MATCHES "PC_GSTREAMER_GL")
                get_target_property(_qt_incs PkgConfig::PC_GSTREAMER_GL INTERFACE_INCLUDE_DIRECTORIES)
                set(__qt_fixed_incs)
                foreach(path IN LISTS _qt_incs)
                    if(IS_DIRECTORY "${path}")
                        list(APPEND __qt_fixed_incs "${path}")
                    endif()
                endforeach()
                set_property(TARGET PkgConfig::PC_GSTREAMER_GL PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${__qt_fixed_incs}")
            endif()
        endif()
    endif()

    if(TARGET ${target})
        set(GStreamer_${component}_FOUND TRUE PARENT_SCOPE)
    endif()
endfunction()

################################################################################

find_gstreamer_component(Core gstreamer-1.0)
find_gstreamer_component(Base gstreamer-base-1.0)
find_gstreamer_component(Video gstreamer-video-1.0)
find_gstreamer_component(Gl gstreamer-gl-1.0)
find_gstreamer_component(GlPrototypes gstreamer-gl-prototypes-1.0)
find_gstreamer_component(Rtsp gstreamer-rtsp-1.0)

################################################################################

if(GlEgl IN_LIST GStreamer_FIND_COMPONENTS)
    find_gstreamer_component(GlEgl gstreamer-gl-egl-1.0)
endif()

if(GlWayland IN_LIST GStreamer_FIND_COMPONENTS)
    find_gstreamer_component(GlWayland gstreamer-gl-wayland-1.0)
endif()

if(GlX11 IN_LIST GStreamer_FIND_COMPONENTS)
    find_gstreamer_component(GlX11 gstreamer-gl-x11-1.0)
endif()

################################################################################

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GStreamer
    VERSION_VAR GStreamer_VERSION
    HANDLE_COMPONENTS
)

if(GStreamer_FOUND AND NOT TARGET GStreamer::GStreamer)
    qt_add_library(GStreamer::GStreamer INTERFACE IMPORTED)

    if(GStreamer_USE_STATIC_LIBS)
        target_compile_definitions(GStreamer::GStreamer INTERFACE QGC_GST_STATIC_BUILD)
    endif()

    if(APPLE AND GStreamer_USE_FRAMEWORK)
        set(CMAKE_FIND_FRAMEWORK ONLY)
        find_library(GSTREAMER_FRAMEWORK GStreamer
            PATHS
                "${GSTREAMER_FRAMEWORK_PATH}"
                "/Library/Frameworks"
                "/usr/local/opt/gstreamer"
                "/opt/homebrew/opt/gstreamer"
        )
        unset(CMAKE_FIND_FRAMEWORK)
        if(GSTREAMER_FRAMEWORK)
            target_link_libraries(GStreamer::GStreamer INTERFACE ${GSTREAMER_FRAMEWORK})
            target_include_directories(GStreamer::GStreamer INTERFACE "${GSTREAMER_FRAMEWORK}/Headers")
            if(MACOS)
                target_compile_definitions(GStreamer::GStreamer INTERFACE QGC_GST_MACOS_FRAMEWORK)
            endif()
            return()
        else()
            message(FATAL_ERROR "Could not locate GStreamer - check installation or set environment/cmake variables")
        endif()
    elseif(ANDROID)
        target_link_options(GStreamer::GStreamer INTERFACE "-Wl,-Bsymbolic")
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            target_link_options(GStreamer::GStreamer INTERFACE "-Wl,-z,notext")
        endif()
    endif()

    target_link_directories(GStreamer::GStreamer INTERFACE ${GSTREAMER_LIB_PATH})

    target_link_libraries(GStreamer::GStreamer
        INTERFACE
            GStreamer::Core
            GStreamer::Base
            GStreamer::Video
            GStreamer::Gl
            GStreamer::GlPrototypes
            GStreamer::Rtsp
    )

    foreach(component IN LISTS GStreamer_FIND_COMPONENTS)
        if(GStreamer_${component}_FOUND)
            target_link_libraries(GStreamer::GStreamer INTERFACE GStreamer::${component})
        endif()
    endforeach()

################################################################################

    if(GStreamer_USE_STATIC_LIBS)
        qt_add_library(GStreamer::Plugins INTERFACE IMPORTED)
        target_link_directories(GStreamer::Plugins INTERFACE ${GSTREAMER_PLUGIN_PATH})

        foreach(plugin IN LISTS GSTREAMER_PLUGINS)
            pkg_check_modules(GST_PLUGIN_${plugin} QUIET IMPORTED_TARGET gst${plugin})
            if(GST_PLUGIN_${plugin}_FOUND)
                target_link_libraries(GStreamer::Plugins INTERFACE PkgConfig::GST_PLUGIN_${plugin})
            else()
                find_library(GST_PLUGIN_${plugin}_LIBRARY
                    NAMES gst${plugin}
                    PATHS
                        ${GSTREAMER_LIB_PATH}
                        ${GSTREAMER_PLUGIN_PATH}
                )
                if(GST_PLUGIN_${plugin}_LIBRARY)
                    target_link_libraries(GStreamer::Plugins INTERFACE ${GST_PLUGIN_${plugin}_LIBRARY})
                    set(GST_PLUGIN_${plugin}_FOUND TRUE)
                endif()
            endif()
            if(GST_PLUGIN_${plugin}_FOUND)
                target_compile_definitions(GStreamer::Plugins INTERFACE GST_PLUGIN_${plugin}_FOUND)
            endif()
        endforeach()

        target_link_libraries(GStreamer::GStreamer INTERFACE GStreamer::Plugins)
    endif()
endif()
