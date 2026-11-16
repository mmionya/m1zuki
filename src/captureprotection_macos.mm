#include "captureprotection_macos.h"
#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QWidget>
#import <AppKit/AppKit.h>

CaptureProtection::CaptureProtection(QObject *parent) : QObject(parent) {}
CaptureProtection::~CaptureProtection() { if (qApp) qApp->removeEventFilter(this); }

void CaptureProtection::configureOverlay(QWidget *widget)
{
    if (!widget || !widget->isWindow() || m_applying
        || QGuiApplication::platformName() != QLatin1String("cocoa")
        || (widget->objectName() != QLatin1String("answerOverlay")
            && widget->objectName() != QLatin1String("errorIndicator"))) return;
    m_applying = true;
    NSView *view = (__bridge NSView *)(reinterpret_cast<void *>(widget->winId()));
    NSWindow *window = view.window;
    if (window) {
        window.ignoresMouseEvents = YES;
        window.hidesOnDeactivate = NO;
        window.level = NSStatusWindowLevel;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
            | NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorStationary;
        if ([window isKindOfClass:[NSPanel class]])
            ((NSPanel *)window).becomesKeyOnlyIfNeeded = YES;
    }
    m_applying = false;
}

void CaptureProtection::start()
{
    if (m_started) return;
    m_started = true;
    qApp->installEventFilter(this);
    for (auto *widget : QApplication::topLevelWidgets()) configureOverlay(widget);
    reportStatus();
}

void CaptureProtection::setAllowUnprotectedOverlay(bool allow)
{
    if (m_allowUnprotected == allow) return;
    m_allowUnprotected = allow;
    if (!allow && m_started)
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QLatin1String("answerOverlay")) widget->hide();
    if (m_started) reportStatus();
}

void CaptureProtection::reportStatus()
{
    emit statusChanged(isReady(), m_allowUnprotected
        ? tr("Оверлей включён. На macOS он может быть виден в записи и демонстрации экрана.")
        : tr("macOS не гарантирует скрытие оверлея из записи. Разрешите показ без защиты в настройках, чтобы продолжить."));
}

bool CaptureProtection::eventFilter(QObject *object, QEvent *event)
{
    if (!m_applying && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange)) {
        if (auto *widget = qobject_cast<QWidget *>(object)) {
            if (event->type() != QEvent::WinIdChange || widget->internalWinId()) configureOverlay(widget);
            if (!m_allowUnprotected && widget->objectName() == QLatin1String("answerOverlay")) widget->hide();
        }
    }
    return QObject::eventFilter(object, event);
}
