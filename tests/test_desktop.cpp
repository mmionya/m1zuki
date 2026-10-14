#include "desktop.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
#include <sys/stat.h>

class DesktopTest : public QObject
{
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> m_directory;
    QByteArray m_previousPath;
    QString m_imagePath;
    QString m_argsPath;
    QString m_outputMarker;

    bool installSpectacle(const QByteArray &operation)
    {
        QFile script(m_directory->filePath(QStringLiteral("spectacle")));
        if (!script.open(QIODevice::WriteOnly))
            return false;
        const QByteArray contents = QByteArrayLiteral(
            "#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > \"$MINIANSWER_TEST_ARGS\"\n"
            "target=''\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  if [ \"$1\" = '-o' ]; then shift; target=\"$1\"; fi\n"
            "  shift\n"
            "done\n"
            "printf '%s' \"$target\" > \"$MINIANSWER_TEST_OUTPUT\"\n") + operation;
        if (script.write(contents) != contents.size())
            return false;
        script.close();
        return script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    QByteArray readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    QString capturePath() { return QString::fromUtf8(readFile(m_outputMarker)); }

private slots:
    void init()
    {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        m_previousPath = qgetenv("PATH");
        m_imagePath = m_directory->filePath(QStringLiteral("synthetic.png"));
        m_argsPath = m_directory->filePath(QStringLiteral("args"));
        m_outputMarker = m_directory->filePath(QStringLiteral("output"));
        QImage image(37, 19, QImage::Format_ARGB32);
        image.fill(QColor(15, 123, 231));
        QVERIFY(image.save(m_imagePath));
        // The real screenshot binary is never reachable from these tests.
        qputenv("PATH", m_directory->path().toUtf8());
        qputenv("MINIANSWER_TEST_IMAGE", m_imagePath.toUtf8());
        qputenv("MINIANSWER_TEST_ARGS", m_argsPath.toUtf8());
        qputenv("MINIANSWER_TEST_OUTPUT", m_outputMarker.toUtf8());
    }

    void cleanup()
    {
        qputenv("PATH", m_previousPath);
        qunsetenv("MINIANSWER_TEST_IMAGE");
        qunsetenv("MINIANSWER_TEST_ARGS");
        qunsetenv("MINIANSWER_TEST_OUTPUT");
        m_directory.reset();
    }

    void successfulCapture_data()
    {
        QTest::addColumn<bool>("currentMonitor");
        QTest::newRow("current-monitor") << true;
        QTest::newRow("all-monitors") << false;
    }

    void successfulCapture()
    {
        QFETCH(bool, currentMonitor);
        QVERIFY(installSpectacle("/bin/cp -- \"$MINIANSWER_TEST_IMAGE\" \"$target\"\n"));
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(currentMonitor);
        QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 3000);
        QCOMPARE(failure.count(), 0);
        const auto image = qvariant_cast<QImage>(success.first().first());
        QCOMPARE(image.size(), QSize(37, 19));
        QCOMPARE(image.pixelColor(0, 0), QColor(15, 123, 231));
        const auto arguments = readFile(m_argsPath).split('\n');
        QVERIFY(arguments.contains("-i"));
        QVERIFY(arguments.contains("-b"));
        QVERIFY(arguments.contains("-n"));
        QVERIFY(arguments.contains(currentMonitor ? "-m" : "-f"));
        QVERIFY(!arguments.contains(currentMonitor ? "-f" : "-m"));
        const auto output = capturePath();
        QVERIFY(!output.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(!QFileInfo::exists(QFileInfo(output).absolutePath()));
    }

    void failedProcessDoesNotEmitImageAndCleansFiles()
    {
        QVERIFY(installSpectacle("/bin/cp -- \"$MINIANSWER_TEST_IMAGE\" \"$target\"\nexit 7\n"));
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(true);
        QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 3000);
        QCOMPARE(success.count(), 0);
        const auto output = capturePath();
        QVERIFY(!output.isEmpty());
        QVERIFY(!QFileInfo::exists(QFileInfo(output).absolutePath()));
    }

    void invalidImageIsRejected()
    {
        QVERIFY(installSpectacle("printf 'not an image' > \"$target\"\n"));
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(true);
        QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 3000);
        QCOMPARE(success.count(), 0);
        QVERIFY(!QFileInfo::exists(QFileInfo(capturePath()).absolutePath()));
    }

    void missingSpectacleFailsWithoutStartingAnything()
    {
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(true);
        QCOMPARE(failure.count(), 1);
        QCOMPARE(success.count(), 0);
        QVERIFY(!QFileInfo::exists(m_outputMarker));
    }

    void cancellationCleansPrivateDirectoryAndAllowsNextCapture()
    {
        QVERIFY(installSpectacle("/bin/cp -- \"$MINIANSWER_TEST_IMAGE\" \"$target\"\nexec /bin/sleep 60\n"));
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(true);
        QTRY_VERIFY_WITH_TIMEOUT(!capturePath().isEmpty(), 3000);
        const auto directory = QFileInfo(capturePath()).absolutePath();
        struct stat mode{};
        QCOMPARE(::stat(QFile::encodeName(directory).constData(), &mode), 0);
        QCOMPARE(mode.st_mode & 0777, mode_t(0700));
        desktop.cancelCapture();
        QVERIFY(!QFileInfo::exists(directory));
        QTest::qWait(40);
        QCOMPARE(success.count(), 0);
        QCOMPARE(failure.count(), 0);
        QVERIFY(installSpectacle("/bin/cp -- \"$MINIANSWER_TEST_IMAGE\" \"$target\"\n"));
        desktop.captureScreen(true);
        QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 3000);
        QCOMPARE(failure.count(), 0);
    }

    void destructionCancelsRunningCapture()
    {
        QVERIFY(installSpectacle("exec /bin/sleep 60\n"));
        auto desktop = std::make_unique<DesktopIntegration>();
        desktop->captureScreen(false);
        QTRY_VERIFY_WITH_TIMEOUT(!capturePath().isEmpty(), 3000);
        const auto directory = QFileInfo(capturePath()).absolutePath();
        QVERIFY(QFileInfo::exists(directory));
        desktop.reset();
        QVERIFY(!QFileInfo::exists(directory));
    }
};

QTEST_MAIN(DesktopTest)
#include "test_desktop.moc"
