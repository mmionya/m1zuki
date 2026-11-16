#pragma once
#include <QObject>
#include <QTimer>

class CaptureProtection : public QObject
{
    Q_OBJECT
public:
    explicit CaptureProtection(QObject *parent = nullptr);
    ~CaptureProtection() override;
    void start();
    bool isReady() const { return m_ready && !m_failed; }
    bool hasFailed() const { return m_failed; }
signals:
    void statusChanged(bool ready, const QString &message);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    bool protect(class QWidget *widget);
    void fail();
    QTimer m_timer;
    bool m_ready = false;
    bool m_failed = false;
    bool m_started = false;
    bool m_applying = false;
};
