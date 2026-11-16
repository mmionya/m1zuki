#pragma once
#include <QObject>

// macOS does not offer a public exclusion guarantee equivalent to KWin or WDA.
// isReady means displaying the overlay was explicitly allowed by the user.
class CaptureProtection : public QObject
{
    Q_OBJECT
public:
    explicit CaptureProtection(QObject *parent = nullptr);
    ~CaptureProtection() override;
    void start();
    void setAllowUnprotectedOverlay(bool allow);
    bool isReady() const { return m_started && m_allowUnprotected; }
    bool hasFailed() const { return m_started && !m_allowUnprotected; }
signals:
    void statusChanged(bool ready, const QString &message);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void reportStatus();
    void configureOverlay(class QWidget *widget);
    bool m_started = false;
    bool m_allowUnprotected = false;
    bool m_applying = false;
};
