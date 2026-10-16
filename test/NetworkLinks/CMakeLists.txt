cmake_minimum_required(VERSION 3.25)
project(NetworkLinksTest VERSION 5.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
if(OHOS)
    find_package(Qt6 REQUIRED COMPONENTS Gui)
    option(QGC_ENABLE_BLUETOOTH "Include Bluetooth bridges in the network package" OFF)
    option(QGC_NO_SERIAL_LINK "Exclude USB serial from the network package" ON)
endif()

# Compile current production workers with real sockets. Configuration/settings
# and LinkInterface application services are excluded from this small target.
function(read_section path first last output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" source)
    string(FIND "${source}" "${first}" start)
    if(start LESS 0)
        message(FATAL_ERROR "Missing production section: ${first}")
    endif()
    string(SUBSTRING "${source}" ${start} -1 remainder)
    string(FIND "${remainder}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Missing production boundary: ${last}")
    endif()
    string(SUBSTRING "${remainder}" 0 ${end} section)
    set(${output} "${section}" PARENT_SCOPE)
endfunction()

set(comms "${CMAKE_CURRENT_LIST_DIR}/../../src/Comms")
read_section("${comms}/UDPLink.h" "struct UDPClient" "class UDPConfiguration" udp_client)
read_section("${comms}/UDPLink.h" "class UDPWorker :" "class UDPLink :" udp_header)
read_section("${comms}/TCPLink.h" "class TCPWorker :" "class TCPLink :" tcp_header)
read_section("${comms}/UDPLink.cc" "namespace {" "UDPConfiguration::UDPConfiguration" udp_helpers)
read_section("${comms}/TCPLink.cc" "namespace {" "TCPConfiguration::TCPConfiguration" tcp_helpers)
read_section("${comms}/UDPLink.cc" "const QHostAddress UDPWorker::_multicastGroup" "UDPLink::UDPLink" udp_source)
read_section("${comms}/TCPLink.cc" "TCPWorker::TCPWorker" "TCPLink::TCPLink" tcp_source)
configure_file(Workers.h.in Workers.h @ONLY)
configure_file(Workers.cc.in Workers.cc @ONLY)

set(test_sources NetworkLinksTest.cc "${CMAKE_CURRENT_BINARY_DIR}/Workers.h")
if(OHOS)
    set(test_target QGroundControl)
    qt_add_executable(${test_target} ${test_sources} "${CMAKE_CURRENT_BINARY_DIR}/Workers.cc")
    get_filename_component(workspace_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    target_link_libraries(${test_target} PRIVATE Qt6::Gui)
    set_target_properties(${test_target} PROPERTIES
        QT_HARMONYOS_APP_BUNDLE_NAME "org.mavlink.qgc.network.tests"
        QT_HARMONYOS_APP_NAME "QGroundControl"
        QT_HARMONYOS_APP_LABEL "QGC Network Tests"
        QT_HARMONYOS_APP_VENDOR "QGroundControl"
        QT_HARMONYOS_APP_ICON "${workspace_root}/resources/icons/android_512x512.png"
        QT_HARMONYOS_APP_VERSION_NAME "5.0.8"
        QT_HARMONYOS_APP_VERSION_CODE 5010513
        QT_HARMONYOS_MODULE_DEVICE_TYPES "tablet;2in1"
        QT_HARMONYOS_COMPATIBLE_SDK_VERSION "6.1.0(23)"
        QT_HARMONYOS_TARGET_ARCHS "${OHOS_ARCH}"
        QT_QML_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    qt_add_harmonyos_permission(${test_target} NAME "ohos.permission.INTERNET")
else()
    set(test_target NetworkLinksTest)
    add_executable(${test_target} ${test_sources} "${CMAKE_CURRENT_BINARY_DIR}/Workers.cc")
    configure_file(Workers.cc.in WorkersHarmony.cc @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/WorkersHarmony.cc"
        PROPERTIES COMPILE_DEFINITIONS Q_OS_HARMONY)
    add_executable(NetworkLinksHarmonyWorkerTest ${test_sources} "${CMAKE_CURRENT_BINARY_DIR}/WorkersHarmony.cc")
    target_compile_definitions(NetworkLinksHarmonyWorkerTest PRIVATE QGC_TEST_HARMONY_WORKER)
    target_include_directories(NetworkLinksHarmonyWorkerTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(NetworkLinksHarmonyWorkerTest PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    if(WIN32)
        target_link_libraries(NetworkLinksHarmonyWorkerTest PRIVATE ws2_32)
    endif()
endif()
target_include_directories(${test_target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(${test_target} PRIVATE Qt6::Core Qt6::Network Qt6::Test)

if(NOT OHOS)
    enable_testing()
    add_test(NAME NetworkLinksTest COMMAND NetworkLinksTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/result-default.txt,txt"
        -o "${CMAKE_CURRENT_BINARY_DIR}/result-default.xml,junitxml")
    add_test(NAME NetworkLinksHarmonyWorkerTest COMMAND NetworkLinksHarmonyWorkerTest
        -o "${CMAKE_CURRENT_BINARY_DIR}/result-harmony.txt,txt"
        -o "${CMAKE_CURRENT_BINARY_DIR}/result-harmony.xml,junitxml")
    set_tests_properties(NetworkLinksTest NetworkLinksHarmonyWorkerTest PROPERTIES TIMEOUT 45)
    set_tests_properties(NetworkLinksTest NetworkLinksHarmonyWorkerTest PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
