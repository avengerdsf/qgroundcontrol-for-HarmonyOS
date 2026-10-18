cmake_minimum_required(VERSION 3.25)
project(WindowStartupTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

# Keep the production window declaration and startup handlers, without loading
# the flight UI and its application services in this small window test.
set(main_window_file "${CMAKE_CURRENT_LIST_DIR}/../../src/MainWindow/MainWindow.qml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${main_window_file}")
file(READ "${main_window_file}" main_window_source)
string(FIND "${main_window_source}" "    QtObject {\n        id: firstRunPromptManager" startup_end)
if(startup_end LESS 0)
    message(FATAL_ERROR "Cannot locate MainWindow startup declaration")
endif()
string(SUBSTRING "${main_window_source}" 0 ${startup_end} startup_source)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MainWindowStartup.qml" "${startup_source}
    SafeArea.additionalMargins.bottom: 28
    QtObject {
        id: firstRunPromptManager
        function nextPrompt() {}
    }
}")

add_executable(WindowStartupTest WindowStartupTest.cc)
target_link_libraries(WindowStartupTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(WindowStartupTest PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}"
    TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

enable_testing()
add_test(NAME WindowStartupTest COMMAND WindowStartupTest)
set_tests_properties(WindowStartupTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")

function(main_window_section first last result)
    string(FIND "${main_window_source}" "${first}" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Cannot locate production plan loading section")
    endif()
    string(SUBSTRING "${main_window_source}" ${begin} -1 remaining)
    string(FIND "${remaining}" "${last}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Cannot locate production plan loading section")
    endif()
    string(SUBSTRING "${remaining}" 0 ${end} content)
    set(${result} "${content}" PARENT_SCOPE)
endfunction()
main_window_section("    function showPlanView()" "    function showTool(" plan_functions)
main_window_section("    function checkForUnsavedMission()" "    function checkForPendingParameterWrites()" plan_close)
main_window_section("    FlyView {" "    footer:" plan_views)
string(REGEX MATCH "    readonly property var planView: [^\n]+\n" plan_alias "${main_window_source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/PlanLoading.qml" "
import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.Controls
import PlanFixtures
Item {
    id: mainWindow
    width: 800; height: 600
    property bool _utmspSendActTrigger: false
    property int _closeChecksToSkip: 0
    property int _skipUnsavedMissionCheckMask: 1
    property string closeDialogTitle: \"Close\"
    property int confirmations: 0
    property bool _reentrantCloseGuard: false
    function showMessageDialog() { confirmations++ }
    function _showMessageDialogWorker() { confirmations++ }
    function performCloseChecks() {}
    QtObject { id: toolDrawer; property bool visible: false }
${plan_alias}
${plan_functions}
${plan_close}
${plan_views}
}")
add_executable(PlanLoadingTest PlanLoadingTest.cc)
target_link_libraries(PlanLoadingTest PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(PlanLoadingTest PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}" TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME PlanLoadingTest COMMAND PlanLoadingTest)
set_tests_properties(PlanLoadingTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
