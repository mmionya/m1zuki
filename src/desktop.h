#pragma once
#include <QtGlobal>
#ifdef Q_OS_WIN
#include "desktop_windows.h"
#elif defined(Q_OS_MACOS)
#include "desktop_macos.h"
#else
#pragma once

#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QTimer>
#include <memory>
#include <optional>

class QAction;
class QProcess;
class QTemporaryDir;
class MouseBindings;

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
    void finishCapture(int exitCode);
    void cleanCapture();

    QAction *m_captureAction;
    QAction *m_clearAction;
    QAction *m_showAction = nullptr;
    QProcess *m_process = nullptr;
    QTimer m_captureTimeout;
    std::unique_ptr<QTemporaryDir> m_captureDirectory;
    QString m_capturePath;
    bool m_shortcutsTouched = false;
    std::unique_ptr<MouseBindings> m_mouseBindings;
    struct ShortcutState {
        QList<QKeySequence> capture;
        QList<QKeySequence> clear;
        int captureMouseButton = 0;
        int clearMouseButton = 0;
    };
    std::optional<ShortcutState> m_previousShortcuts;
    int m_captureMouseButton = 0;
    int m_clearMouseButton = 0;
};

#endif
