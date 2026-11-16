#include "mousebindings.h"

#include <KConfig>
#include <KConfigGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdlib>

namespace {
const QKeySequence captureKeys(QStringLiteral("Ctrl+Alt+F9"));
const QKeySequence clearKeys(QStringLiteral("Ctrl+Alt+F10"));

QString configPath()
{
    return QDir(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME"))).filePath(QStringLiteral("kcminputrc"));
}

QStringList keyBinding(const QKeySequence &keys)
{
    return {QStringLiteral("Key"), keys.toString(QKeySequence::PortableText)};
}

QStringList mouseEntry(const QString &key)
{
    KConfig config(configPath(), KConfig::SimpleConfig);
    return config.group(QStringLiteral("ButtonRebinds")).group(QStringLiteral("Mouse"))
        .readEntry(key, QStringList{});
}

QMap<QString, QString> mouseEntries()
{
    KConfig config(configPath(), KConfig::SimpleConfig);
    return config.group(QStringLiteral("ButtonRebinds")).group(QStringLiteral("Mouse")).entryMap();
}

bool writeMouseEntry(const QString &key, const QStringList &value)
{
    KConfig config(configPath(), KConfig::SimpleConfig);
    config.group(QStringLiteral("ButtonRebinds")).group(QStringLiteral("Mouse")).writeEntry(key, value);
    return config.sync();
}
}

class MouseBindingsTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QByteArray m_previousConfigHome;
    bool m_hadConfigHome = false;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
        m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
        // Set this before the first KConfig instance; no desktop settings are touched.
        qputenv("XDG_CONFIG_HOME", m_directory.path().toUtf8());
    }

    void cleanupTestCase()
    {
        if (m_hadConfigHome)
            qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
        else
            qunsetenv("XDG_CONFIG_HOME");
    }

    void init()
    {
        QVERIFY(QDir(m_directory.path()).removeRecursively());
        QVERIFY(QDir().mkpath(m_directory.path()));
    }

    void nativeMappingPreservesOtherSettings()
    {
        const QStringList unrelated{QStringLiteral("Key"), QStringLiteral("Alt+Left")};
        QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton3"), unrelated));
        {
            KConfig config(configPath(), KConfig::SimpleConfig);
            config.group(QStringLiteral("Mouse")).writeEntry(QStringLiteral("cursorTheme"), QStringLiteral("existing-theme"));
            QVERIFY(config.sync());
        }
        {
            MouseBindings mouse;
            QString error = QStringLiteral("old error");
            QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 5, &error), qPrintable(error));
            QVERIFY(error.isEmpty());
            QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(captureKeys));
            QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), keyBinding(clearKeys));
            QCOMPARE(mouseEntry(QStringLiteral("ExtraButton3")), unrelated);
        }
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), QStringList{});
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), QStringList{});
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton3")), unrelated);
        KConfig config(configPath(), KConfig::SimpleConfig);
        QCOMPARE(config.group(QStringLiteral("Mouse")).readEntry(QStringLiteral("cursorTheme")), QStringLiteral("existing-theme"));
    }

    void switchingAndDisablingRestoresOriginalMappings()
    {
        const QStringList previousBack{QStringLiteral("Key"), QStringLiteral("Alt+Left")};
        const QStringList previousForward{QStringLiteral("MouseButton"), QStringLiteral("274")};
        QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton1"), previousBack));
        QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton2"), previousForward));
        MouseBindings mouse;
        QString error;
        QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 0, &error), qPrintable(error));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(captureKeys));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), previousForward);

        QVERIFY2(mouse.configure(captureKeys, clearKeys, 0, 4, &error), qPrintable(error));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(clearKeys));

        QVERIFY2(mouse.configure(captureKeys, clearKeys, 5, 0, &error), qPrintable(error));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), previousBack);
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), keyBinding(captureKeys));

        QVERIFY2(mouse.configure(captureKeys, clearKeys, 0, 0, &error), qPrintable(error));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), previousBack);
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), previousForward);
    }

    void middleButtonBindingIsInstalledAndRestored()
    {
        const QStringList previousMiddle{QStringLiteral("MouseButton"), QStringLiteral("2")};
        QVERIFY(writeMouseEntry(QStringLiteral("MiddleButton"), previousMiddle));
        {
            MouseBindings mouse;
            QString error;
            QVERIFY2(mouse.configure(captureKeys, clearKeys, 2, 0, &error), qPrintable(error));
            QCOMPARE(mouseEntry(QStringLiteral("MiddleButton")), keyBinding(captureKeys));
        }
        QCOMPARE(mouseEntry(QStringLiteral("MiddleButton")), previousMiddle);
    }

    void changedKeyboardShortcutUpdatesMapping()
    {
        MouseBindings mouse;
        QString error;
        QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 5, &error), qPrintable(error));
        const QKeySequence replacement(QStringLiteral("Meta+F8"));
        QVERIFY2(mouse.configure(replacement, clearKeys, 4, 5, &error), qPrintable(error));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(replacement));
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), keyBinding(clearKeys));
    }

    void destructorRestoresAllButtonAliases()
    {
        QVERIFY(writeMouseEntry(QStringLiteral("BackButton"), {QStringLiteral("Key"), QStringLiteral("Alt+Left")}));
        QVERIFY(writeMouseEntry(QStringLiteral("XButton1"), {QStringLiteral("Disabled")}));
        QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton1"), {QStringLiteral("Key"), QStringLiteral("Ctrl+B")}));
        QVERIFY(writeMouseEntry(QStringLiteral("ForwardButton"), {QStringLiteral("Key"), QStringLiteral("Alt+Right")}));
        QVERIFY(writeMouseEntry(QStringLiteral("XButton2"), {QStringLiteral("Disabled")}));
        const auto original = mouseEntries();
        {
            MouseBindings mouse;
            QString error;
            QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 5, &error), qPrintable(error));
            QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(captureKeys));
            QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), keyBinding(clearKeys));
            QVERIFY(!mouseEntries().contains(QStringLiteral("BackButton")));
            QVERIFY(!mouseEntries().contains(QStringLiteral("XButton1")));
            QVERIFY(!mouseEntries().contains(QStringLiteral("ForwardButton")));
            QVERIFY(!mouseEntries().contains(QStringLiteral("XButton2")));
        }
        QCOMPARE(mouseEntries(), original);
    }

    void externalModificationSurvivesDestruction()
    {
        QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton1"), {QStringLiteral("Key"), QStringLiteral("Alt+Left")}));
        const QStringList externalBinding{QStringLiteral("Key"), QStringLiteral("Ctrl+T")};
        {
            MouseBindings mouse;
            QString error;
            QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 5, &error), qPrintable(error));
            // Emulate changing the mapping in KDE settings while m1zuki is open.
            QVERIFY(writeMouseEntry(QStringLiteral("ExtraButton1"), externalBinding));
        }
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), externalBinding);
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton2")), QStringList{});
    }

    void externalAliasDoesNotLeaveOurBindingBehind()
    {
        const QStringList externalBinding{QStringLiteral("Key"), QStringLiteral("Ctrl+T")};
        {
            MouseBindings mouse;
            QVERIFY(mouse.configure(captureKeys, clearKeys, 4, 0));
            QVERIFY(writeMouseEntry(QStringLiteral("BackButton"), externalBinding));
        }
        QCOMPARE(mouseEntry(QStringLiteral("BackButton")), externalBinding);
        QVERIFY(!mouseEntries().contains(QStringLiteral("ExtraButton1")));
    }

    void invalidButtonsAreAtomic_data()
    {
        QTest::addColumn<int>("captureButton");
        QTest::addColumn<int>("clearButton");
        QTest::newRow("negative") << -1 << 0;
        QTest::newRow("left") << 1 << 0;
        QTest::newRow("middle") << 0 << 3;
        QTest::newRow("extra") << 6 << 0;
        QTest::newRow("qt-enum-instead-of-mouse-number") << 8 << 0;
        QTest::newRow("duplicate-back") << 4 << 4;
        QTest::newRow("duplicate-forward") << 5 << 5;
        QTest::newRow("duplicate-middle") << 2 << 2;
    }

    void invalidButtonsAreAtomic()
    {
        QFETCH(int, captureButton);
        QFETCH(int, clearButton);
        MouseBindings mouse;
        QString error;
        QVERIFY2(mouse.configure(captureKeys, clearKeys, 4, 5, &error), qPrintable(error));
        const auto previous = mouseEntries();
        QVERIFY(!mouse.configure(captureKeys, clearKeys, captureButton, clearButton, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(mouseEntries(), previous);
    }

    void nextLaunchRecoversAfterCrash()
    {
        QVERIFY(writeMouseEntry(QStringLiteral("BackButton"), {QStringLiteral("Key"), QStringLiteral("Alt+Left")}));
        const auto original = mouseEntries();
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--leave-mouse-binding")});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished());
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(mouseEntry(QStringLiteral("ExtraButton1")), keyBinding(captureKeys));
        {
            MouseBindings mouse;
            QString error;
            // Recovery must also work when no new keyboard shortcuts can be registered.
            QVERIFY2(mouse.recover(&error), qPrintable(error));
        }
        QCOMPARE(mouseEntries(), original);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--leave-mouse-binding"))) {
        // Terminate without destructors to exercise persistent crash recovery.
        MouseBindings mouse;
        const bool configured = mouse.configure(captureKeys, clearKeys, 4, 0);
        std::_Exit(configured ? 0 : 1);
    }
    MouseBindingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mousebindings.moc"
