target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        QGCSerialPortInfoTest.cc
        QGCSerialPortInfoTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(QGC_PLATFORM_OHOS AND NOT QGC_NO_SERIAL_LINK)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonySerialBridgeTest.cc HarmonySerialBridgeTest.h)
endif()

if(QGC_PLATFORM_OHOS AND QGC_ENABLE_BLUETOOTH)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE HarmonyBluetoothBridgeTest.cc HarmonyBluetoothBridgeTest.h)
endif()
