#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#else
#include "windowspermissions.h"
#endif

class SettingsTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir temporaryDirectory;
    QByteArray previousConfigHome;
    QByteArray previousApiKey;
    bool hadConfigHome = false;
    bool hadApiKey = false;

    bool writeConfig(const QByteArray &contents)
    {
        if (!QDir().mkpath(QFileInfo(settingsFilePath()).absolutePath()))
            return false;
        QFile file(settingsFilePath());
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(contents) == contents.size();
    }

private slots:
    void unprotectedOverlayRequiresExplicitBooleanOptIn()
    {
        QVERIFY(writeConfig("{}"));
        QVERIFY(!loadSettings().allowUnprotectedOverlay);
        QVERIFY(writeConfig(R"({"allowUnprotectedOverlay":"true"})"));
        QString error;
        QVERIFY(!loadSettings(&error).allowUnprotectedOverlay);
        QVERIFY(!error.isEmpty());
        AppSettings settings;
        settings.allowUnprotectedOverlay = true;
        QVERIFY(saveSettings(settings));
        QVERIFY(loadSettings().allowUnprotectedOverlay);
    }
    void initTestCase()
    {
        QVERIFY(temporaryDirectory.isValid());
        hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        previousConfigHome = qgetenv("XDG_CONFIG_HOME");
        hadApiKey = qEnvironmentVariableIsSet("OPENROUTER_API_KEY");
        previousApiKey = qgetenv("OPENROUTER_API_KEY");
        qputenv("XDG_CONFIG_HOME", temporaryDirectory.path().toUtf8());
        QCOMPARE(settingsFilePath(), temporaryDirectory.filePath(QStringLiteral("mini-answer/settings.json")));
    }

    void cleanupTestCase()
    {
        if (hadConfigHome)
            qputenv("XDG_CONFIG_HOME", previousConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");
        if (hadApiKey)
            qputenv("OPENROUTER_API_KEY", previousApiKey);
        else
            qunsetenv("OPENROUTER_API_KEY");
    }

    void init()
    {
        QDir(QFileInfo(settingsFilePath()).absolutePath()).removeRecursively();
        qunsetenv("OPENROUTER_API_KEY");
    }

    void missingFileUsesDefaultsAndClearsError()
    {
        QString error = QStringLiteral("old error");
        const auto settings = loadSettings(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(settings.model, AppSettings{}.model);
        QCOMPARE(settings.displaySeconds, 15);
        QCOMPARE(settings.captureMouseButton, 0);
        QCOMPARE(settings.clearMouseButton, 0);
        QVERIFY(settings.apiKey.isEmpty());
        QVERIFY(!QFileInfo::exists(settingsFilePath()));
    }

    void roundTripAndPrivatePermissions()
    {
        AppSettings settings;
        settings.apiKey = QStringLiteral("test-key-never-log");
        settings.model = QStringLiteral("qwen/qwen3-4b:free");
        settings.captureShortcut = QStringLiteral("Ctrl+Shift+F9");
        settings.clearShortcut = QStringLiteral("Ctrl+Shift+F10");
        settings.captureMouseButton = 4;
        settings.clearMouseButton = 5;
        settings.displaySeconds = 300;
        settings.fontPixels = 96;
        settings.currentMonitor = false;
        QString error;
        QVERIFY2(saveSettings(settings, &error), qPrintable(error));

        const auto loaded = loadSettings(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.apiKey, settings.apiKey);
        QCOMPARE(loaded.model, settings.model);
        QCOMPARE(loaded.captureShortcut, settings.captureShortcut);
        QCOMPARE(loaded.clearShortcut, settings.clearShortcut);
        QCOMPARE(loaded.captureMouseButton, settings.captureMouseButton);
        QCOMPARE(loaded.clearMouseButton, settings.clearMouseButton);
        QCOMPARE(loaded.displaySeconds, settings.displaySeconds);
        QCOMPARE(loaded.fontPixels, settings.fontPixels);
        QCOMPARE(loaded.currentMonitor, settings.currentMonitor);

#ifndef Q_OS_WIN
        struct stat status{};
        QCOMPARE(::stat(QFile::encodeName(settingsFilePath()).constData(), &status), 0);
        QCOMPARE(status.st_mode & 0777, mode_t(0600));
        QCOMPARE(::stat(QFile::encodeName(QFileInfo(settingsFilePath()).absolutePath()).constData(), &status), 0);
        QCOMPARE(status.st_mode & 0777, mode_t(0700));
#else
        // Inspect the ACL created by saveSettings, without repairing it in the test.
        for (const auto &path : {settingsFilePath(), QFileInfo(settingsFilePath()).absolutePath()}) {
            auto native = QDir::toNativeSeparators(path).toStdWString();
            PACL acl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            QCOMPARE(GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                         nullptr, nullptr, &acl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
            QVERIFY(acl);
            QCOMPARE(acl->AceCount, WORD(2));
            HANDLE token = nullptr;
            QVERIFY(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
            DWORD size = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            QByteArray buffer(size, '\0');
            QVERIFY(GetTokenInformation(token, TokenUser, buffer.data(), size, &size));
            CloseHandle(token);
            auto *user = reinterpret_cast<TOKEN_USER *>(buffer.data());
            for (DWORD i = 0; i < acl->AceCount; ++i) {
                void *raw = nullptr;
                QVERIFY(GetAce(acl, i, &raw));
                const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
                QCOMPARE(ace->Header.AceType, BYTE(ACCESS_ALLOWED_ACE_TYPE));
                PSID sid = const_cast<DWORD *>(&ace->SidStart);
                QVERIFY(EqualSid(sid, user->User.Sid) || IsWellKnownSid(sid, WinLocalSystemSid));
            }
            LocalFree(descriptor);
        }
#endif
    }

    void environmentFallbackIsNotWrittenToConfig()
    {
        qputenv("OPENROUTER_API_KEY", QByteArrayLiteral("environment-secret"));
        AppSettings settings;
        QCOMPARE(effectiveApiKey(settings), QStringLiteral("environment-secret"));
        QVERIFY(saveSettings(settings));
        QFile file(settingsFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("environment-secret"));
        settings.apiKey = QStringLiteral("explicit-secret");
        QCOMPARE(effectiveApiKey(settings), QStringLiteral("explicit-secret"));
        settings.apiKey.clear();
        qunsetenv("OPENROUTER_API_KEY");
        QVERIFY(effectiveApiKey(settings).isEmpty());
    }

    void partialConfigurationKeepsDefaults()
    {
        QVERIFY(writeConfig(R"({"model":"openrouter/auto","futureSetting":42})"));
        QString error;
        const auto settings = loadSettings(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(settings.model, QStringLiteral("openrouter/auto"));
        QCOMPARE(settings.fontPixels, 32);
        QCOMPARE(settings.captureMouseButton, 0);
        QCOMPARE(settings.clearMouseButton, 0);
        QVERIFY(settings.currentMonitor);
    }

    void invalidConfiguration_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("broken-json") << QByteArrayLiteral("{\"apiKey\":\"private-secret");
        QTest::newRow("array-root") << QByteArrayLiteral("[]");
        QTest::newRow("key-number") << QByteArrayLiteral(R"({"apiKey":12})");
        QTest::newRow("key-whitespace") << QByteArrayLiteral(R"({"apiKey":"private secret"})");
        QTest::newRow("model-empty") << QByteArrayLiteral(R"({"model":""})");
        QTest::newRow("model-invalid") << QByteArrayLiteral(R"({"model":"provider/model?bad"})");
        QTest::newRow("model-trailing-slash") << QByteArrayLiteral(R"({"model":"provider/model/"})");
        QTest::newRow("model-object") << QByteArrayLiteral(R"({"model":{}})");
        QTest::newRow("capture-invalid") << QByteArrayLiteral(R"({"captureShortcut":"not-a-key"})");
        QTest::newRow("capture-sequence") << QByteArrayLiteral(R"({"captureShortcut":"Ctrl+A, Ctrl+B"})");
        QTest::newRow("clear-empty") << QByteArrayLiteral(R"({"clearShortcut":""})");
        QTest::newRow("identical-shortcuts") << QByteArrayLiteral(R"({"clearShortcut":"Ctrl+Alt+Space"})");
        QTest::newRow("mouse-negative") << QByteArrayLiteral(R"({"captureMouseButton":-1})");
        QTest::newRow("mouse-primary") << QByteArrayLiteral(R"({"captureMouseButton":1})");
        QTest::newRow("mouse-secondary") << QByteArrayLiteral(R"({"clearMouseButton":3})");
        QTest::newRow("mouse-large") << QByteArrayLiteral(R"({"clearMouseButton":6})");
        QTest::newRow("mouse-fraction") << QByteArrayLiteral(R"({"captureMouseButton":4.5})");
        QTest::newRow("mouse-string") << QByteArrayLiteral(R"({"clearMouseButton":"5"})");
        QTest::newRow("mouse-null") << QByteArrayLiteral(R"({"captureMouseButton":null})");
        QTest::newRow("identical-mouse-back") << QByteArrayLiteral(R"({"captureMouseButton":4,"clearMouseButton":4})");
        QTest::newRow("identical-mouse-forward") << QByteArrayLiteral(R"({"captureMouseButton":5,"clearMouseButton":5})");
        QTest::newRow("seconds-negative") << QByteArrayLiteral(R"({"displaySeconds":-1})");
        QTest::newRow("seconds-zero") << QByteArrayLiteral(R"({"displaySeconds":0})");
        QTest::newRow("seconds-large") << QByteArrayLiteral(R"({"displaySeconds":301})");
        QTest::newRow("seconds-fraction") << QByteArrayLiteral(R"({"displaySeconds":1.5})");
        QTest::newRow("seconds-string") << QByteArrayLiteral(R"({"displaySeconds":"15"})");
        QTest::newRow("font-small") << QByteArrayLiteral(R"({"fontPixels":11})");
        QTest::newRow("font-large") << QByteArrayLiteral(R"({"fontPixels":97})");
        QTest::newRow("font-null") << QByteArrayLiteral(R"({"fontPixels":null})");
        QTest::newRow("monitor-string") << QByteArrayLiteral(R"({"currentMonitor":"true"})");
        QTest::newRow("monitor-null") << QByteArrayLiteral(R"({"currentMonitor":null})");
        QTest::newRow("overlarge-file") << QByteArray(65537, ' ');
    }

    void invalidConfiguration()
    {
        QFETCH(QByteArray, contents);
        QVERIFY(writeConfig(contents));
        QString error;
        const auto settings = loadSettings(&error);
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains(QStringLiteral("private-secret")));
        QCOMPARE(settings.model, AppSettings{}.model);
        QCOMPARE(settings.apiKey, QString{});
        QCOMPARE(settings.displaySeconds, 15);
        QCOMPARE(settings.fontPixels, 32);
        QCOMPARE(settings.captureMouseButton, 0);
        QCOMPARE(settings.clearMouseButton, 0);
        QVERIFY(settings.currentMonitor);
    }

    void invalidSavePreservesExistingFile()
    {
        AppSettings settings;
        settings.apiKey = QStringLiteral("original-key");
        QVERIFY(saveSettings(settings));
        QFile original(settingsFilePath());
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto originalContents = original.readAll();
        original.close();

        settings.model.clear();
        settings.apiKey = QStringLiteral("new-private-secret");
        QString error;
        QVERIFY(!saveSettings(settings, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains(settings.apiKey));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalContents);
    }

    void mouseBindingsRoundTrip_data()
    {
        QTest::addColumn<int>("captureButton");
        QTest::addColumn<int>("clearButton");
        QTest::newRow("disabled") << 0 << 0;
        QTest::newRow("capture-back") << 4 << 0;
        QTest::newRow("capture-forward") << 5 << 0;
        QTest::newRow("capture-wheel-click") << 2 << 0;
        QTest::newRow("clear-back") << 0 << 4;
        QTest::newRow("clear-forward") << 0 << 5;
        QTest::newRow("clear-wheel-click") << 0 << 2;
        QTest::newRow("both") << 4 << 5;
        QTest::newRow("both-reversed") << 5 << 4;
        QTest::newRow("wheel-click-and-back") << 2 << 4;
    }

    void mouseBindingsRoundTrip()
    {
        QFETCH(int, captureButton);
        QFETCH(int, clearButton);
        AppSettings settings;
        settings.captureMouseButton = captureButton;
        settings.clearMouseButton = clearButton;
        QString error;
        QVERIFY2(saveSettings(settings, &error), qPrintable(error));
        const auto loaded = loadSettings(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.captureMouseButton, captureButton);
        QCOMPARE(loaded.clearMouseButton, clearButton);
    }

    void invalidMouseSavePreservesExistingFile_data()
    {
        QTest::addColumn<int>("captureButton");
        QTest::addColumn<int>("clearButton");
        QTest::newRow("negative") << -1 << 0;
        QTest::newRow("primary") << 1 << 0;
        QTest::newRow("secondary") << 0 << 3;
        QTest::newRow("large") << 0 << 6;
        QTest::newRow("duplicate-back") << 4 << 4;
        QTest::newRow("duplicate-forward") << 5 << 5;
        QTest::newRow("duplicate-wheel-click") << 2 << 2;
    }

    void invalidMouseSavePreservesExistingFile()
    {
        QFETCH(int, captureButton);
        QFETCH(int, clearButton);
        AppSettings settings;
        QVERIFY(saveSettings(settings));
        QFile file(settingsFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto originalContents = file.readAll();
        file.close();

        settings.captureMouseButton = captureButton;
        settings.clearMouseButton = clearButton;
        QString error;
        QVERIFY(!saveSettings(settings, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), originalContents);
    }

#ifndef Q_OS_WIN
    void saveRepairsLoosePermissions()
    {
        QVERIFY(writeConfig("{}"));
        const auto directory = QFileInfo(settingsFilePath()).absolutePath();
        QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                 | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther));
        QVERIFY(QFile::setPermissions(settingsFilePath(), QFile::ReadOwner | QFile::WriteOwner
                                                          | QFile::ReadGroup | QFile::ReadOther));
        QVERIFY(saveSettings(AppSettings{}));
        struct stat status{};
        QCOMPARE(::stat(QFile::encodeName(settingsFilePath()).constData(), &status), 0);
        QCOMPARE(status.st_mode & 0777, mode_t(0600));
        QCOMPARE(::stat(QFile::encodeName(directory).constData(), &status), 0);
        QCOMPARE(status.st_mode & 0777, mode_t(0700));
    }
#endif
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "test_settings.moc"
