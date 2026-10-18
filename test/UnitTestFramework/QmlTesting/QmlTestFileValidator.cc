#include "QmlTestFileValidator.h"

#include <QtCore/QDir>

#include "QmlTesting.h"

// Register as standalone - quick_test_main calls exit() so can't run with other tests
UT_REGISTER_TEST_STANDALONE(QmlTestFileValidator, TestLabel::Unit)

QmlTestFileValidator::QmlTestFileValidator() {}

QString QmlTestFileValidator::_testDirectory() const
{
#if defined(Q_OS_HARMONY)
    const QDir testDir(QStringLiteral(":/QGC/Tests/QmlTesting"));
#else
    const QDir testDir(QStringLiteral(QGC_QML_TEST_SOURCE_DIR));
#endif
    if (testDir.exists()) {
        return testDir.absolutePath();
    }

    return QString();
}

void QmlTestFileValidator::_runQmlTests()
{
    // Qt Quick Test's quick_test_main() calls exit(), so it cannot run inside the
    // normal test harness. QML tests must be built and invoked as a separate
    // executable via CTest. This runner validates that the test files are present
    // and structurally valid, but actual QML execution happens out-of-process.
    const QString testDir = _testDirectory();
#if defined(Q_OS_HARMONY)
    QVERIFY2(!testDir.isEmpty(), "Packaged QML test directory not found");
#else
    if (testDir.isEmpty()) {
        QSKIP("QML test directory not found (quick_test_main requires a separate executable)");
        return;
    }
#endif

    const QDir dir(testDir);
    const QStringList testFiles = dir.entryList({"tst_*.qml"}, QDir::Files);
#if defined(Q_OS_HARMONY)
    QVERIFY2(!testFiles.isEmpty(), "No packaged QML test files found");
#else
    if (testFiles.isEmpty()) {
        QSKIP("No QML test files found");
        return;
    }
#endif

    // Validate structure of discovered QML test files
    for (const QString& file : testFiles) {
        const QString path = dir.absoluteFilePath(file);
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable("Cannot open: " + path));
        const QByteArray content = f.readAll();
        QVERIFY2(content.contains("TestCase"), qPrintable("Missing TestCase in: " + file));
        QVERIFY2(content.contains("function test_"), qPrintable("No test functions in: " + file));
    }
}
