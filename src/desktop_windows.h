#pragma once
#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <optional>
#include <qt_windows.h>

class DesktopIntegration : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit DesktopIntegration(QObject *parent = nullptr);
    ~DesktopIntegration() override;
    bool registerShortcuts(const QKeySequence &capture, const QKeySequence &clear,
                           int captureMouseButton = 0, int clearMouseButton = 0,
                           QString *error = nullptr);
    bool registerShowShortcut(QString *error = nullptr);
    bool rollbackShortcuts(QString *error = nullptr);
    void captureScreen(bool currentMonitor);
    void cancelCapture();
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
signals:
    void captureRequested();
    void clearRequested();
    void showRequested();
    void captured(const QImage &image);
    void failed(const QString &message);
private:
    friend class DesktopWindowsTest;
    struct State { QKeySequence capture, clear; int captureMouse = 0, clearMouse = 0; };
    bool apply(const State &state, QString *error);
    static LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data);
    static DesktopIntegration *s_mouseOwner;
    State m_state;
    std::optional<State> m_previous;
    bool m_showRegistered = false;
    HWND m_window = nullptr;
    HHOOK m_hook = nullptr;
    unsigned m_generation = 0;
    unsigned m_bindingGeneration = 0;
    bool m_capturing = false;
};
