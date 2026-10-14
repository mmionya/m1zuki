#pragma once

#include <QDBusContext>
#include <QObject>
#include <QTimer>
#include <memory>

class QDBusServiceWatcher;
class QTemporaryDir;

class CaptureProtection : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.minianswer.CaptureProtection")
public:
    explicit CaptureProtection(QObject *parent = nullptr);
    ~CaptureProtection() override;

    void start();
    bool isReady() const;
    bool hasFailed() const { return m_failed; }

signals:
    void statusChanged(bool ready, const QString &message);

public slots:
    Q_SCRIPTABLE void report(const QString &token, bool ready);

private:
    void fail(const QString &message);
    void unloadScript();
    void ownerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);

    QDBusServiceWatcher *m_serviceWatcher;
    QTimer m_startupTimeout;
    std::unique_ptr<QTemporaryDir> m_scriptDirectory;
    QString m_kwinOwner;
    QString m_pluginName;
    QString m_token;
    bool m_objectRegistered = false;
    bool m_starting = false;
    bool m_ready = false;
    bool m_failed = false;
    bool m_requested = false;
};
