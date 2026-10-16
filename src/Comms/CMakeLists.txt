target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        LinkConfiguration.cc
        LinkConfiguration.h
        LinkInterface.cc
        LinkInterface.h
        LinkManager.cc
        LinkManager.h
        LogReplayLink.cc
        LogReplayLink.h
        LogReplayLinkController.cc
        LogReplayLinkController.h
        MAVLinkProtocol.cc
        MAVLinkProtocol.h
        TCPLink.cc
        TCPLink.h
        UDPLink.cc
        UDPLink.h
        UdpIODevice.cc
        UdpIODevice.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QABSTRACTSOCKET_DEBUG)

#===========================================================================#

if(QGC_PLATFORM_OHOS AND (QGC_ENABLE_BLUETOOTH OR NOT QGC_NO_SERIAL_LINK))
    find_path(NodeAddonApi_INCLUDE_DIR NAMES napi.h
        HINTS "${CMAKE_SOURCE_DIR}/tmp/vcpkg/installed/x64-ohos/include"
        NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    include("${Qt6_DIR}/FindNodeAddonApi.cmake")
    set_property(TARGET NodeAddonApi::NodeAddonApi PROPERTY IMPORTED_GLOBAL TRUE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE NodeAddonApi::NodeAddonApi ace_napi.z)
endif()

if(QGC_NO_SERIAL_LINK)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_NO_SERIAL_LINK)
else()
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            QGCSerialPortInfo.cc
            QGCSerialPortInfo.h
            SerialLink.cc
            SerialLink.h
    )

    if(QGC_PLATFORM_OHOS)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            HarmonySerialBridge.cc HarmonySerialBridge.h
            HarmonySerialIODevice.cc HarmonySerialIODevice.h)
        set_source_files_properties(HarmonySerialBridge.cc PROPERTIES
            COMPILE_DEFINITIONS "NODE_ADDON_API_ENABLE_TYPE_CHECK_ON_AS;NAPI_CPP_EXCEPTIONS")
    endif()

    if(NOT ANDROID AND NOT IOS)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::SerialPort)
    endif()
endif()

#===========================================================================#

if(QGC_ENABLE_BLUETOOTH)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Bluetooth)
    target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
            BluetoothLink.cc
            BluetoothLink.h
    )
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_ENABLE_BLUETOOTH)
    if(QGC_PLATFORM_OHOS)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonyBluetoothBridge.cc HarmonyBluetoothBridge.h)
        set_source_files_properties(HarmonyBluetoothBridge.cc PROPERTIES
            COMPILE_DEFINITIONS "NODE_ADDON_API_ENABLE_TYPE_CHECK_ON_AS;NAPI_CPP_EXCEPTIONS")
    endif()
endif()

#===========================================================================#

if(QGC_ZEROCONF_ENABLED)
    CPMAddPackage(
        NAME qmdnsengine
        GITHUB_REPOSITORY nitroshare/qmdnsengine
        GIT_TAG master
        OPTIONS
            "BUILD_DOC OFF"
            "BUILD_EXAMPLES OFF"
            "BUILD_TESTS OFF"
    )

    if(TARGET qmdnsengine)
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE qmdnsengine)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_ZEROCONF_ENABLED)
    endif()
endif()

#===========================================================================#

add_subdirectory(AirLink)
add_subdirectory(MockLink)
