#include "desktop_macos.h"
#include <QSignalSpy>
#include <QtTest>

class MacDesktopTest : public QObject {
    Q_OBJECT
private slots:
    void invalidBindingsDoNotRequestMousePermissions()
    {
        DesktopIntegration desktop;
        QString error;
        QVERIFY(!desktop.registerShortcuts({}, {}, 0, 0, &error));
        QVERIFY(!error.isEmpty());
        const QKeySequence capture("Ctrl+Shift+F16"), clear("Ctrl+Shift+F17");
        QVERIFY(!desktop.registerShortcuts(capture, capture, 0, 0, &error));
        QVERIFY(!desktop.registerShortcuts(QKeySequence("Ctrl+Alt+M"), clear, 0, 0, &error));
        QVERIFY(!desktop.registerShortcuts(capture, clear, 4, 4, &error));
        QVERIFY(!desktop.registerShortcuts(capture, clear, 1, 0, &error));
        QVERIFY(!desktop.registerShortcuts(QKeySequence("Ctrl+A, Ctrl+B"), clear, 0, 0, &error));
        QVERIFY(desktop.rollbackShortcuts(&error));
        QVERIFY(error.isEmpty());
    }
    void cancellationWithoutCaptureIsSilent()
    {
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.cancelCapture();
        desktop.cancelCapture();
        QTest::qWait(20);
        QCOMPARE(success.count(), 0);
        QCOMPARE(failure.count(), 0);
    }
    void keyboardBindingsRollbackAndRelease()
    {
        const QKeySequence capture("Ctrl+Shift+F16"), clear("Ctrl+Shift+F17");
        {
            DesktopIntegration desktop;
            QString error;
            if (!desktop.registerShortcuts(capture, clear, 0, 0, &error))
                QSKIP(qPrintable(QString("WindowServer/global hotkeys unavailable: ") + error));
            QVERIFY(desktop.registerShortcuts(clear, capture, 0, 0, &error));
            QVERIFY(desktop.rollbackShortcuts(&error));
            DesktopIntegration other;
            QVERIFY(!other.registerShortcuts(capture, clear, 0, 0, &error));
        }
        DesktopIntegration next;
        QVERIFY(next.registerShortcuts(capture, clear));
        QVERIFY(next.rollbackShortcuts());
        DesktopIntegration other;
        QVERIFY(other.registerShortcuts(capture, clear));
    }
};
QTEST_MAIN(MacDesktopTest)
#include "test_desktop_macos.moc"
