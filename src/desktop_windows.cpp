#include "desktop.h"
#include <QCoreApplication>
#include <QTimer>
#include <array>

namespace {
struct Hotkey { UINT modifiers = MOD_NOREPEAT; UINT key = 0; };
Hotkey translate(const QKeySequence &sequence)
{
    Hotkey result;
    if (sequence.count() != 1) return result;
    const auto combination = sequence[0];
    const auto mods = combination.keyboardModifiers();
    if (mods & Qt::ControlModifier) result.modifiers |= MOD_CONTROL;
    if (mods & Qt::AltModifier) result.modifiers |= MOD_ALT;
    if (mods & Qt::ShiftModifier) result.modifiers |= MOD_SHIFT;
    if (mods & Qt::MetaModifier) result.modifiers |= MOD_WIN;
    const int key = combination.key();
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) result.key = VK_F1 + key - Qt::Key_F1;
    else if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) result.key = key;
    else switch (key) {
    case Qt::Key_Space: result.key = VK_SPACE; break;
    case Qt::Key_Backspace: result.key = VK_BACK; break;
    case Qt::Key_Tab: result.key = VK_TAB; break;
    case Qt::Key_Return: case Qt::Key_Enter: result.key = VK_RETURN; break;
    case Qt::Key_Escape: result.key = VK_ESCAPE; break;
    case Qt::Key_Delete: result.key = VK_DELETE; break;
    case Qt::Key_Insert: result.key = VK_INSERT; break;
    case Qt::Key_Home: result.key = VK_HOME; break;
    case Qt::Key_End: result.key = VK_END; break;
    case Qt::Key_PageUp: result.key = VK_PRIOR; break;
    case Qt::Key_PageDown: result.key = VK_NEXT; break;
    case Qt::Key_Left: result.key = VK_LEFT; break;
    case Qt::Key_Right: result.key = VK_RIGHT; break;
    case Qt::Key_Up: result.key = VK_UP; break;
    case Qt::Key_Down: result.key = VK_DOWN; break;
    case Qt::Key_Pause: result.key = VK_PAUSE; break;
    case Qt::Key_Print: result.key = VK_SNAPSHOT; break;
    default:
        if (key > 0 && key < 0x10000) {
            const SHORT mapped = VkKeyScanW(static_cast<WCHAR>(key));
            if (mapped != -1) {
                result.key = LOBYTE(mapped);
                if (HIBYTE(mapped) & 1) result.modifiers |= MOD_SHIFT;
                if (HIBYTE(mapped) & 2) result.modifiers |= MOD_CONTROL;
                if (HIBYTE(mapped) & 4) result.modifiers |= MOD_ALT;
            }
        }
    }
    if (mods & Qt::KeypadModifier) {
        if (key >= '0' && key <= '9') result.key = VK_NUMPAD0 + key - '0';
        else if (key == Qt::Key_Plus) result.key = VK_ADD;
        else if (key == Qt::Key_Minus) result.key = VK_SUBTRACT;
        else if (key == Qt::Key_Asterisk) result.key = VK_MULTIPLY;
        else if (key == Qt::Key_Slash) result.key = VK_DIVIDE;
        else if (key == Qt::Key_Period) result.key = VK_DECIMAL;
    }
    return result;
}
bool validButton(int button) { return button == 0 || button == 2 || button == 4 || button == 5; }
QImage screenshot(bool currentMonitor)
{
    RECT rect{};
    if (currentMonitor) {
        POINT cursor{};
        if (!GetCursorPos(&cursor)) return {};
        MONITORINFO monitor{sizeof(MONITORINFO), {}, {}, 0};
        if (!GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor)) return {};
        rect = monitor.rcMonitor;
    } else {
        rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    const int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return {};
    HDC screen = GetDC(nullptr);
    if (!screen) return {};
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    QImage image;
    if (memory && bitmap && pixels) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT))
            image = QImage(static_cast<uchar *>(pixels), width, height, QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return image;
}
}

DesktopIntegration *DesktopIntegration::s_mouseOwner = nullptr;
DesktopIntegration::DesktopIntegration(QObject *parent) : QObject(parent)
{
    m_window = CreateWindowExW(0, L"STATIC", L"m1zuki hotkeys", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    QCoreApplication::instance()->installNativeEventFilter(this);
}
DesktopIntegration::~DesktopIntegration()
{
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (m_hook) UnhookWindowsHookEx(m_hook);
    if (s_mouseOwner == this) s_mouseOwner = nullptr;
    if (m_window) { UnregisterHotKey(m_window, 1); UnregisterHotKey(m_window, 2); UnregisterHotKey(m_window, 3); DestroyWindow(m_window); }
}
bool DesktopIntegration::registerShowShortcut(QString *error)
{
    if (error) error->clear();
    if (m_showRegistered) return true;
    if (!m_window || !RegisterHotKey(m_window, 3, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'M')) {
        if (error) *error = tr("Ctrl+Alt+M занято другой программой. Освободите сочетание и попробуйте скрыть значок снова.");
        return false;
    }
    m_showRegistered = true;
    return true;
}
bool DesktopIntegration::apply(const State &state, QString *error)
{
    if (error) error->clear();
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const auto capture = translate(state.capture), clear = translate(state.clear);
    const auto reserved = [](const Hotkey &key) {
        return key.key == 'M' && key.modifiers == (MOD_CONTROL | MOD_ALT | MOD_NOREPEAT);
    };
    if (reserved(capture) || reserved(clear))
        return fail(tr("Ctrl+Alt+M используется для возвращения окна m1zuki. Выберите другое сочетание."));
    if (!capture.key || !clear.key || (capture.key == clear.key && capture.modifiers == clear.modifiers))
        return fail(tr("Выберите два разных сочетания клавиш, поддерживаемых Windows."));
    if (!validButton(state.captureMouse) || !validButton(state.clearMouse)
        || (state.captureMouse && state.captureMouse == state.clearMouse))
        return fail(tr("Выберите разные кнопки мыши: среднюю, Mouse4 или Mouse5."));
    if (!m_window) return fail(tr("Не удалось создать обработчик горячих клавиш Windows."));
    HHOOK newHook = nullptr;
    if ((state.captureMouse || state.clearMouse) && !m_hook) {
        if (s_mouseOwner && s_mouseOwner != this) return fail(tr("Обработчик мыши уже используется."));
        newHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
        if (!newHook) return fail(tr("Windows не разрешила назначить кнопки мыши."));
    }
    UnregisterHotKey(m_window, 1); UnregisterHotKey(m_window, 2);
    if (!RegisterHotKey(m_window, 1, capture.modifiers, capture.key)
        || !RegisterHotKey(m_window, 2, clear.modifiers, clear.key)) {
        const auto code = GetLastError();
        UnregisterHotKey(m_window, 1); UnregisterHotKey(m_window, 2);
        const auto oldCapture = translate(m_state.capture), oldClear = translate(m_state.clear);
        bool restored = true;
        if (oldCapture.key) restored &= bool(RegisterHotKey(m_window, 1, oldCapture.modifiers, oldCapture.key));
        if (oldClear.key) restored &= bool(RegisterHotKey(m_window, 2, oldClear.modifiers, oldClear.key));
        if (newHook) UnhookWindowsHookEx(newHook);
        return fail(tr("Не удалось назначить горячие клавиши (Windows %1). Сочетание может быть занято.%2")
                    .arg(code).arg(restored ? QString{} : tr(" Прежние клавиши восстановить не удалось.")));
    }
    if (newHook) { m_hook = newHook; s_mouseOwner = this; }
    if (!state.captureMouse && !state.clearMouse && m_hook) {
        UnhookWindowsHookEx(m_hook); m_hook = nullptr; s_mouseOwner = nullptr;
    }
    m_state = state;
    ++m_bindingGeneration;
    return true;
}
bool DesktopIntegration::registerShortcuts(const QKeySequence &capture, const QKeySequence &clear,
                                           int captureMouseButton, int clearMouseButton, QString *error)
{
    const auto previous = m_state;
    if (!apply({capture, clear, captureMouseButton, clearMouseButton}, error)) return false;
    m_previous = previous;
    return true;
}
bool DesktopIntegration::rollbackShortcuts(QString *error)
{
    if (error) error->clear();
    if (!m_previous) return true;
    if (m_previous->capture.isEmpty() && m_previous->clear.isEmpty()) {
        UnregisterHotKey(m_window, 1); UnregisterHotKey(m_window, 2);
        if (m_hook) { UnhookWindowsHookEx(m_hook); m_hook = nullptr; s_mouseOwner = nullptr; }
        m_state = *m_previous;
        ++m_bindingGeneration;
    } else if (!apply(*m_previous, error)) return false;
    m_previous.reset();
    return true;
}
bool DesktopIntegration::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY || msg->hwnd != m_window) return false;
    if (msg->wParam == 1) emit captureRequested();
    else if (msg->wParam == 2) emit clearRequested();
    else if (msg->wParam == 3 && m_showRegistered) emit showRequested();
    else return false;
    if (result) *result = 0;
    return true;
}
LRESULT CALLBACK DesktopIntegration::mouseHook(int code, WPARAM message, LPARAM data)
{
    auto *owner = s_mouseOwner;
    if (code == HC_ACTION && owner) {
        const auto *event = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
        const bool down = message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
        const bool up = message == WM_MBUTTONUP || message == WM_XBUTTONUP;
        const int button = (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) ? 2
            : ((message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
               ? (HIWORD(event->mouseData) == XBUTTON1 ? 4 : 5) : 0);
        if (!(event->flags & LLMHF_INJECTED) && button
            && (button == owner->m_state.captureMouse || button == owner->m_state.clearMouse)) {
            if (down) {
                const bool capture = button == owner->m_state.captureMouse;
                const auto generation = owner->m_bindingGeneration;
                QTimer::singleShot(0, owner, [owner, capture, generation] {
                    if (generation != owner->m_bindingGeneration) return;
                    if (capture) emit owner->captureRequested(); else emit owner->clearRequested();
                });
            }
            if (down || up) return 1;
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
void DesktopIntegration::captureScreen(bool currentMonitor)
{
    if (m_capturing) { emit failed(tr("Предыдущий снимок экрана ещё создаётся.")); return; }
    m_capturing = true;
    const auto generation = ++m_generation;
    QTimer::singleShot(0, this, [this, generation, currentMonitor] {
        if (generation != m_generation) return;
        const auto image = screenshot(currentMonitor);
        m_capturing = false;
        if (image.isNull()) emit failed(tr("Windows не вернула снимок экрана."));
        else emit captured(image);
    });
}
void DesktopIntegration::cancelCapture() { ++m_generation; m_capturing = false; }
