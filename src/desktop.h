#pragma once

#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QTimer>
#include <memory>

class QAction;
class QProcess;
class QTemporaryDir;

class DesktopIntegration : public QObject
{
    Q_OBJECT
public:
    explicit DesktopIntegration(QObject *parent = nullptr);
    ~DesktopIntegration() override;

    bool registerShortcuts(const QKeySequence &capture, const QKeySequence &clear,
                           QString *error = nullptr);
    void captureScreen(bool currentMonitor);
    void cancelCapture();

signals:
    void captureRequested();
    void clearRequested();
    void captured(const QImage &image);
    void failed(const QString &message);

private:
    void finishCapture(int exitCode);
    void cleanCapture();

    QAction *m_captureAction;
    QAction *m_clearAction;
    QProcess *m_process = nullptr;
    QTimer m_captureTimeout;
    std::unique_ptr<QTemporaryDir> m_captureDirectory;
    QString m_capturePath;
    bool m_shortcutsTouched = false;
};
