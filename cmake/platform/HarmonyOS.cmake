set(QGC_HARMONYOS_VERSION_CODE 5011501 CACHE STRING "HarmonyOS application version code")

find_package(Qt6 REQUIRED COMPONENTS HarmonyExtrasPrivate)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::HarmonyExtrasPrivate)

set_target_properties(${CMAKE_PROJECT_NAME}
    PROPERTIES
        QT_HARMONYOS_APP_BUNDLE_NAME "${QGC_PACKAGE_NAME}"
        QT_HARMONYOS_APP_NAME "${QGC_APP_NAME}"
        QT_HARMONYOS_APP_LABEL "${QGC_APP_NAME}"
        QT_HARMONYOS_APP_VENDOR "${QGC_ORG_NAME}"
        QT_HARMONYOS_APP_ICON "${CMAKE_SOURCE_DIR}/resources/icons/android_512x512.png"
        QT_HARMONYOS_APP_VERSION_NAME "${PROJECT_VERSION}"
        QT_HARMONYOS_APP_VERSION_CODE "${QGC_HARMONYOS_VERSION_CODE}"
        QT_HARMONYOS_MODULE_DEVICE_TYPES "phone;tablet;2in1"
        QT_HARMONYOS_COMPATIBLE_SDK_VERSION "6.1.0(23)"
        QT_HARMONYOS_TARGET_ARCHS "${OHOS_ARCH}"
        QT_QML_ROOT_PATH "${CMAKE_SOURCE_DIR}/src"
)

qt_add_harmonyos_permission(${CMAKE_PROJECT_NAME} NAME "ohos.permission.INTERNET")
qt_add_harmonyos_permission(${CMAKE_PROJECT_NAME} NAME "ohos.permission.WINDOW_TOPMOST")
qt_add_harmonyos_permission(${CMAKE_PROJECT_NAME}
    NAME "ohos.permission.ACCESS_BLUETOOTH"
    REASON "$string:qgc_bluetooth_reason"
    USED_SCENE_ABILITIES "QAbility"
    USED_SCENE_WHEN "inuse")
