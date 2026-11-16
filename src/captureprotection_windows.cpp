#include "captureprotection.h"
#include <QApplication>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QWidget>
#include <qt_windows.h>
#include <dwmapi.h>

namespace { constexpr DWORD excludeFromCapture = 0x00000011; }
CaptureProtection::CaptureProtection(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        BOOL composition = FALSE;
        if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) { fail(); return; }
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (widget->isVisible() && !protect(widget)) { fail(); return; }
        }
    });
}
CaptureProtection::~CaptureProtection() { qApp->removeEventFilter(this); }
void CaptureProtection::fail()
{
    if (m_failed) return;
    m_ready = false; m_failed = true;
    m_timer.stop();
    emit statusChanged(false, tr("Windows не подтвердила исключение окон из записи. Ответ скрыт."));
}
bool CaptureProtection::protect(QWidget *widget)
{
    if (!widget || !widget->isWindow() || m_applying) return true;
    m_applying = true;
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    DWORD affinity = 0;
    const bool ok = SetWindowDisplayAffinity(hwnd, excludeFromCapture)
        && GetWindowDisplayAffinity(hwnd, &affinity) && affinity == excludeFromCapture;
    m_applying = false;
    return ok;
}
void CaptureProtection::start()
{
    if (m_started) return;
    m_started = true;
    // Qt's fade/roll effects create temporary snapshot windows. Avoid both
    // unprotected snapshots and affinity checks during their destruction.
    for (const auto effect : {Qt::UI_FadeTooltip, Qt::UI_AnimateTooltip,
                              Qt::UI_FadeMenu, Qt::UI_AnimateMenu, Qt::UI_AnimateCombo})
        QApplication::setEffectEnabled(effect, false);
    const auto version = QOperatingSystemVersion::current();
    BOOL composition = FALSE;
    if (version < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 19041)
        || FAILED(DwmIsCompositionEnabled(&composition)) || !composition) { fail(); return; }
    qApp->installEventFilter(this);
    for (auto *widget : QApplication::topLevelWidgets())
        if (!protect(widget)) { fail(); return; }
    m_ready = true;
    m_timer.start();
    emit statusChanged(true, tr("Окна исключены из записи средствами Windows."));
}
bool CaptureProtection::eventFilter(QObject *object, QEvent *event)
{
    if (!m_applying && (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show)) {
        if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow()) {
            // WinIdChange also fires while Qt destroys a native window.
            // Calling winId() there would recreate a half-destroyed HWND.
            if (event->type() == QEvent::WinIdChange && !widget->internalWinId())
                return QObject::eventFilter(object, event);
            if (!protect(widget)) {
                fail();
                if (widget->objectName() == QLatin1String("answerOverlay")) widget->hide();
            }
        }
    }
    return QObject::eventFilter(object, event);
}
