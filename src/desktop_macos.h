#pragma once
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>

class DesktopIntegration : public QObject
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
signals:
    void captureRequested();
    void clearRequested();
    void showRequested();
    void captured(const QImage &image);
    void failed(const QString &message);
private:
    struct Native;
    struct State { QKeySequence capture, clear; int captureMouse = 0, clearMouse = 0; };
    bool apply(const State &state, QString *error);
    void finishCapture(unsigned generation, const QImage &image, const QString &error);
    std::unique_ptr<Native> m_native;
    State m_state;
    std::optional<State> m_previous;
    QTimer m_timeout;
    unsigned m_generation = 0;
    unsigned m_bindingGeneration = 0;
    bool m_capturing = false;
};
