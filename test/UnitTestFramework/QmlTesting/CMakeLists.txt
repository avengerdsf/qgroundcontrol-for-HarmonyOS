# ----------------------------------------------------------------------------
# Qt Quick Test (QML Testing)
# ----------------------------------------------------------------------------
if(TARGET Qt6::QuickTest)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::QuickTest)
else()
    message(WARNING "Qt6::QuickTest not found - QML testing disabled")
    return()
endif()

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/QmlTesting.h
        ${CMAKE_CURRENT_SOURCE_DIR}/QmlTestFileValidator.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/QmlTestFileValidator.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Desktop tests read directly from the source tree. HarmonyOS embeds the same
# files in both test libraries because the host source tree is unavailable.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QGC_QML_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")

# Dedicated out-of-process Qt Quick Test executable.
# This runs tst_*.qml files through quick_test_main(), which cannot run
# in-process inside the standard UnitTest harness.
if(QGC_PLATFORM_OHOS)
    qt_add_executable(QGCQmlQuickTests QmlQuickTestMain.cc)
else()
    add_executable(QGCQmlQuickTests QmlQuickTestMain.cc)
endif()

target_link_libraries(QGCQmlQuickTests
    PRIVATE
        Qt6::QuickTest
        Qt6::Qml
        Qt6::Gui
        Qt6::Quick
)

if(QGC_PLATFORM_OHOS)
    target_compile_definitions(QGCQmlQuickTests PRIVATE QUICK_TEST_SOURCE_DIR=":/QGC/Tests/QmlTesting")
    foreach(_qml_test_target ${CMAKE_PROJECT_NAME} QGCQmlQuickTests)
        qt_add_resources(${_qml_test_target} "${_qml_test_target}_qml_tests"
            PREFIX "/QGC/Tests/QmlTesting"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/tst_Example.qml"
        )
    endforeach()
    set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY QT_HARMONYOS_EXTRA_LIBS QGCQmlQuickTests)
    add_dependencies(${CMAKE_PROJECT_NAME} QGCQmlQuickTests)
    set(_qml_quick_test_command ${QGC_HARMONYOS_TEST_RUNNER}
        --bundle-name "${QGC_PACKAGE_NAME}.tests"
        --no-progress-timeout 0
        --timeout 900
        --test-env QT_QUICK_BACKEND=software
        -- $<TARGET_FILE:QGCQmlQuickTests>
    )
else()
    target_compile_definitions(QGCQmlQuickTests PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set(_qml_quick_test_command $<TARGET_FILE:QGCQmlQuickTests>)
endif()

if(NOT QGC_TEST_ONSCREEN)
    list(APPEND _qml_quick_test_command -platform offscreen)
endif()

add_test(
    NAME QmlQuickTests
    COMMAND ${_qml_quick_test_command}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_tests_properties(QmlQuickTests PROPERTIES
    LABELS "Unit;QML"
    TIMEOUT ${QGC_TEST_TIMEOUT_UNIT}
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_LOGGING_RULES=*.debug=false;QML2_IMPORT_PATH=${CMAKE_BINARY_DIR}/qml"
    FAIL_REGULAR_EXPRESSION "FAIL!;QFATAL;Segmentation fault;ASSERT"
)

if(QGC_PLATFORM_OHOS)
    set_tests_properties(QmlQuickTests PROPERTIES TIMEOUT 950 RUN_SERIAL TRUE)
endif()

foreach(_check_target check check-unit check-fast check-ci)
    if(TARGET ${_check_target})
        add_dependencies(${_check_target} QGCQmlQuickTests)
    endif()
endforeach()

add_qgc_test(QmlTestFileValidator LABELS Unit)
