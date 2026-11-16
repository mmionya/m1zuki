#include "desktop.h"
#include <QSignalSpy>
#include <QtTest>
#include <qt_windows.h>

class DesktopWindowsTest : public QObject {
    Q_OBJECT
private slots:
    void rebindingDiscardsQueuedMouseActions()
    {
        DesktopIntegration desktop;
        const QKeySequence capture("Ctrl+Alt+Shift+F9"), clear("Ctrl+Alt+Shift+F10");
        QVERIFY(desktop.registerShortcuts(capture, clear, 2, 5));
        QSignalSpy requested(&desktop, &DesktopIntegration::captureRequested);
        MSLLHOOKSTRUCT event{};
        DesktopIntegration::mouseHook(HC_ACTION, WM_MBUTTONDOWN, reinterpret_cast<LPARAM>(&event));
        QVERIFY(desktop.registerShortcuts(capture, clear, 0, 0));
        QTest::qWait(30);
        QCOMPARE(requested.count(), 0);
        QVERIFY(desktop.rollbackShortcuts());
        DesktopIntegration::mouseHook(HC_ACTION, WM_MBUTTONDOWN, reinterpret_cast<LPARAM>(&event));
        QTRY_COMPARE(requested.count(), 1);
    }
    void restoreShortcutConflictsAndReleases()
    {
        {
            DesktopIntegration desktop;
            QString error;
            QVERIFY2(desktop.registerShowShortcut(&error), qPrintable(error));
            QVERIFY(desktop.registerShowShortcut(&error));
            DesktopIntegration other;
            QVERIFY(!other.registerShowShortcut(&error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!desktop.registerShortcuts(QKeySequence("Ctrl+Alt+M"), QKeySequence("Ctrl+Shift+F10")));
            QSignalSpy show(&desktop, &DesktopIntegration::showRequested);
            QVERIFY(PostMessageW(desktop.m_window, WM_HOTKEY, 3, 0));
            QTRY_COMPARE(show.count(), 1);
        }
        DesktopIntegration next;
        QVERIFY(next.registerShowShortcut());
    }
    void validatesAndRollsBack()
    {
        DesktopIntegration desktop;
        QString error;
        QVERIFY(!desktop.registerShortcuts({}, {}, 0, 0, &error));
        QVERIFY(!error.isEmpty());
        const QKeySequence capture("Ctrl+Alt+Shift+F9"), clear("Ctrl+Alt+Shift+F10");
        QVERIFY2(desktop.registerShortcuts(capture, clear, 0, 0, &error), qPrintable(error));
        QVERIFY(!desktop.registerShortcuts(capture, capture, 0, 0, &error));
        QVERIFY(!desktop.registerShortcuts(capture, clear, 4, 4, &error));
        QVERIFY(!desktop.registerShortcuts(capture, clear, 1, 0, &error));
        QVERIFY2(desktop.registerShortcuts(clear, capture, 4, 5, &error), qPrintable(error));
        QVERIFY2(desktop.rollbackShortcuts(&error), qPrintable(error));
        // A competing registration must not destroy either existing binding.
        DesktopIntegration other;
        QVERIFY(!other.registerShortcuts(capture, clear, 0, 0, &error));
        QVERIFY2(desktop.registerShortcuts(capture, clear, 0, 0, &error), qPrintable(error));
    }
    void releasesOnDestruction()
    {
        const QKeySequence capture("Ctrl+Alt+Shift+F9"), clear("Ctrl+Alt+Shift+F10");
        { DesktopIntegration desktop; QVERIFY(desktop.registerShortcuts(capture, clear)); }
        DesktopIntegration next;
        QVERIFY(next.registerShortcuts(capture, clear));
        QVERIFY(next.rollbackShortcuts());
        DesktopIntegration other;
        QVERIFY(other.registerShortcuts(capture, clear));
    }
    void cancellationPreventsCapture()
    {
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(true);
        desktop.cancelCapture();
        QTest::qWait(50);
        QCOMPARE(success.count(), 0);
        QCOMPARE(failure.count(), 0);
    }
    void capturesPhysicalMonitorBounds_data()
    {
        QTest::addColumn<bool>("currentMonitor");
        QTest::newRow("cursor-monitor") << true;
        QTest::newRow("virtual-desktop") << false;
    }
    void capturesPhysicalMonitorBounds()
    {
        QFETCH(bool, currentMonitor);
        DesktopIntegration desktop;
        QSignalSpy success(&desktop, &DesktopIntegration::captured);
        QSignalSpy failure(&desktop, &DesktopIntegration::failed);
        desktop.captureScreen(currentMonitor);
        QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 3000);
        QCOMPARE(failure.count(), 0);
        const auto image = qvariant_cast<QImage>(success.first().first());
        QSize expected(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
        if (currentMonitor) {
            POINT cursor{}; QVERIFY(GetCursorPos(&cursor));
            MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
            QVERIFY(GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor));
            expected = QSize(monitor.rcMonitor.right - monitor.rcMonitor.left,
                             monitor.rcMonitor.bottom - monitor.rcMonitor.top);
        }
        QCOMPARE(image.size(), expected);
    }
    void mouseBindingsDispatchAndConsumeOnlyAssignedButtons()
    {
        DesktopIntegration desktop;
        QVERIFY(desktop.registerShortcuts(QKeySequence("Ctrl+Alt+Shift+F9"), QKeySequence("Ctrl+Alt+Shift+F10"), 2, 5));
        QSignalSpy capture(&desktop, &DesktopIntegration::captureRequested);
        QSignalSpy clear(&desktop, &DesktopIntegration::clearRequested);
        MSLLHOOKSTRUCT event{};
        QCOMPARE(DesktopIntegration::mouseHook(HC_ACTION, WM_MBUTTONDOWN, reinterpret_cast<LPARAM>(&event)), LRESULT(1));
        QCOMPARE(DesktopIntegration::mouseHook(HC_ACTION, WM_MBUTTONUP, reinterpret_cast<LPARAM>(&event)), LRESULT(1));
        event.mouseData = DWORD(XBUTTON2) << 16;
        QCOMPARE(DesktopIntegration::mouseHook(HC_ACTION, WM_XBUTTONDOWN, reinterpret_cast<LPARAM>(&event)), LRESULT(1));
        QTRY_COMPARE(capture.count(), 1);
        QTRY_COMPARE(clear.count(), 1);
        event.mouseData = DWORD(XBUTTON1) << 16;
        DesktopIntegration::mouseHook(HC_ACTION, WM_XBUTTONDOWN, reinterpret_cast<LPARAM>(&event));
        event.flags = LLMHF_INJECTED;
        DesktopIntegration::mouseHook(HC_ACTION, WM_MBUTTONDOWN, reinterpret_cast<LPARAM>(&event));
        QTest::qWait(20);
        QCOMPARE(capture.count(), 1);
        QCOMPARE(clear.count(), 1);
    }
    void dispatchesNativeHotkey()
    {
        DesktopIntegration desktop;
        QVERIFY(desktop.registerShortcuts(QKeySequence("Ctrl+Alt+Shift+F9"), QKeySequence("Ctrl+Alt+Shift+F10")));
        QSignalSpy capture(&desktop, &DesktopIntegration::captureRequested);
        QSignalSpy clear(&desktop, &DesktopIntegration::clearRequested);
        HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, L"STATIC", L"m1zuki hotkeys");
        QVERIFY(hwnd);
        MSG message{}; message.hwnd = hwnd; message.message = WM_HOTKEY; message.wParam = 1;
        qintptr result = -1;
        QVERIFY(desktop.nativeEventFilter({}, &message, &result));
        QCOMPARE(capture.count(), 1);
        message.wParam = 2;
        QVERIFY(desktop.nativeEventFilter({}, &message, &result));
        QCOMPARE(clear.count(), 1);
        message.hwnd = nullptr;
        QVERIFY(!desktop.nativeEventFilter({}, &message, &result));
        QVERIFY(PostMessageW(hwnd, WM_HOTKEY, 1, 0));
        QTRY_COMPARE_WITH_TIMEOUT(capture.count(), 2, 1000);
    }
};
QTEST_MAIN(DesktopWindowsTest)
#include "test_desktop_windows.moc"
