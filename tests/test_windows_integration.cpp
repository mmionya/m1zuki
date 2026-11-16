#include "captureprotection.h"
#include "overlay.h"
#include "controller.h"
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QComboBox>
#include <QToolTip>
#include <QPointer>
#include <QLineEdit>
#include <QApplication>
#include <QSignalSpy>
#include <QWidget>
#include <QtTest>
#include <qt_windows.h>

class WindowsIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void controllerReleasesBothOverlayWindows()
    {
        auto *controller = new Controller(true);
        QPointer<Overlay> answer;
        QPointer<Overlay> error;
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QLatin1String("answerOverlay")) answer = qobject_cast<Overlay *>(widget);
            if (widget->objectName() == QLatin1String("errorIndicator")) error = qobject_cast<Overlay *>(widget);
        }
        QVERIFY(answer);
        QVERIFY(error);
        delete controller;
        QVERIFY(answer.isNull());
        QVERIFY(error.isNull());
    }
    void invalidSettingsReplaceAnswerWithErrorIndicator()
    {
        QTemporaryDir config;
        const auto previous = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        Controller controller;
        qputenv("XDG_CONFIG_HOME", previous);
        controller.handleCommand("demo");
        Overlay *answer = nullptr;
        Overlay *error = nullptr;
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QLatin1String("answerOverlay")) answer = qobject_cast<Overlay *>(widget);
            if (widget->objectName() == QLatin1String("errorIndicator")) error = qobject_cast<Overlay *>(widget);
        }
        QVERIFY(answer);
        QVERIFY(error);
        QTRY_VERIFY(answer->isVisible());
        // Locate by the explicit model placeholder, independent of child order.
        QLineEdit *model = nullptr;
        for (auto *edit : controller.findChildren<QLineEdit *>())
            if (edit->placeholderText() == QLatin1String("provider/model")) model = edit;
        QVERIFY(model);
        model->setText("invalid");
        auto *save = controller.findChild<QPushButton *>("primary");
        QVERIFY(save);
        save->click();
        QVERIFY(!answer->isVisible());
        QVERIFY(error->isVisible());
        controller.handleCommand("clear");
        QVERIFY(!error->isVisible());
    }
    void transientWindowsAndDestruction()
    {
        QWidget root;
        QComboBox combo(&root);
        combo.addItems({"None", "Middle", "Back"});
        root.show();
        CaptureProtection protection;
        QSignalSpy status(&protection, &CaptureProtection::statusChanged);
        protection.start();
        QVERIFY(protection.isReady());
        combo.showPopup();
        QTest::qWait(100);
        QVERIFY2(protection.isReady(), qPrintable(status.last()[1].toString()));
        combo.hidePopup();
        QToolTip::showText(root.mapToGlobal(QPoint(20, 20)), "test tooltip", &root);
        QTest::qWait(1100); // Include the periodic affinity verification.
        QVERIFY2(protection.isReady(), qPrintable(status.last()[1].toString()));
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (!widget->isVisible()) continue;
            DWORD affinity = 0;
            QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(widget->winId()), &affinity));
            QCOMPARE(affinity, DWORD(0x11));
        }
        QToolTip::hideText();
        auto *temporary = new QWidget;
        temporary->show();
        delete temporary;
        QTest::qWait(1100);
        QVERIFY2(protection.isReady(), qPrintable(status.last()[1].toString()));
    }
    void hideTrayAndRestoreWindow()
    {
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const auto previous = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        Controller controller;
        qputenv("XDG_CONFIG_HOME", previous);
        controller.show();
        auto *button = controller.findChild<QPushButton *>("trayVisibilityButton");
        auto *tray = controller.findChild<QSystemTrayIcon *>();
        QVERIFY(button);
        QVERIFY(tray);
        QVERIFY(tray->isVisible());
        button->click();
        QVERIFY(!tray->isVisible());
        QVERIFY(!controller.isVisible());
        controller.handleCommand("capture"); // No configured API key: local error only.
        Overlay *indicator = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QLatin1String("errorIndicator"))
                indicator = qobject_cast<Overlay *>(widget);
        QVERIFY(indicator);
        QVERIFY(indicator->isVisible());
        QVERIFY(!controller.isVisible());
        QVERIFY(!tray->isVisible());
        controller.handleCommand("clear");
        QVERIFY(!indicator->isVisible());
        const HWND hwnd = FindWindowExW(HWND_MESSAGE, nullptr, L"STATIC", L"m1zuki hotkeys");
        QVERIFY(hwnd);
        QVERIFY(PostMessageW(hwnd, WM_HOTKEY, 3, 0));
        QTRY_VERIFY(controller.isVisible());
        QVERIFY(!tray->isVisible());
        controller.close();
        QVERIFY(!controller.isVisible());
        QVERIFY(PostMessageW(hwnd, WM_HOTKEY, 3, 0));
        QTRY_VERIFY(controller.isVisible());
        button->click();
        QVERIFY(tray->isVisible());
    }
    void protectsExistingAndNewWindows()
    {
        QWidget settings;
        Overlay overlay;
        CaptureProtection protection;
        QSignalSpy status(&protection, &CaptureProtection::statusChanged);
        protection.start();
        QVERIFY2(protection.isReady(), status.isEmpty() ? "No protection status" : qPrintable(status.last()[1].toString()));
        DWORD affinity = 0;
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(overlay.winId()), &affinity));
        QCOMPARE(affinity, DWORD(0x11));
        const HWND previousFocus = GetForegroundWindow();
        overlay.showQuestionAnswers({{1, 3}, {2}}, 1, 32);
        QTest::qWait(100);
        QVERIFY(overlay.isVisible());
        QCOMPARE(GetForegroundWindow(), previousFocus);
        const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(overlay.winId()), GWL_EXSTYLE);
        QVERIFY(style & WS_EX_TRANSPARENT);
        QVERIFY(style & WS_EX_NOACTIVATE);
        QVERIFY(style & WS_EX_TOPMOST);
        QWidget later;
        later.setAttribute(Qt::WA_ShowWithoutActivating);
        later.show();
        QTest::qWait(50);
        QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(later.winId()), &affinity));
        QCOMPARE(affinity, DWORD(0x11));
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 2000);
    }
};
QTEST_MAIN(WindowsIntegrationTest)
#include "test_windows_integration.moc"
