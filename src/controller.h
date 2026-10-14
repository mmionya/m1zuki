#pragma once

#include "settings.h"
#include <QWidget>

class DesktopIntegration;
class OpenRouterClient;
class Overlay;
class CaptureProtection;
class QLineEdit;
class QKeySequenceEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QSystemTrayIcon;

class Controller : public QWidget {
    Q_OBJECT
public:
    explicit Controller(bool demoMode = false);
    void handleCommand(const QString &command);
    bool renderPreview(const QString &path);
    bool configured() const;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void setupTray();
    bool save();
    void applyShortcuts();
    void capture();
    void clear();
    void showSettings();
    void status(const QString &message, bool error = false);
    void preview();
    AppSettings settings_;
    DesktopIntegration *desktop_;
    OpenRouterClient *client_;
    Overlay *overlay_;
    CaptureProtection *protection_;
    QSystemTrayIcon *tray_ = nullptr;
    QLineEdit *keyEdit_;
    QLineEdit *modelEdit_;
    QKeySequenceEdit *captureEdit_;
    QKeySequenceEdit *clearEdit_;
    QSpinBox *secondsEdit_;
    QSpinBox *fontEdit_;
    QCheckBox *monitorEdit_;
    QLabel *statusLabel_;
    QString protectionStatus_;
    bool busy_ = false;
    bool demoMode_ = false;
    bool previewPending_ = false;
    quint64 generation_ = 0;
};
