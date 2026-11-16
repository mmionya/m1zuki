#include "desktop_macos.h"
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QPixmap>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <iterator>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace {
constexpr OSType hotkeySignature = 0x4d317a75; // M1zu
struct Hotkey { UInt32 key = 0, modifiers = 0; bool valid = false; };
Hotkey translate(const QKeySequence &sequence)
{
    Hotkey result;
    if (sequence.count() != 1) return result;
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    // Qt deliberately maps portable Ctrl to Command and Meta to physical Control.
    if (modifiers & Qt::ControlModifier) result.modifiers |= cmdKey;
    if (modifiers & Qt::MetaModifier) result.modifiers |= controlKey;
    if (modifiers & Qt::AltModifier) result.modifiers |= optionKey;
    if (modifiers & Qt::ShiftModifier) result.modifiers |= shiftKey;
    const int key = combination.key();
    const UInt32 functionKeys[] = {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6,
        kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
        kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) {
        result.key = functionKeys[key - Qt::Key_F1]; result.valid = true; return result;
    }
    const auto named = [&result](UInt32 code) { result.key = code; result.valid = true; };
    switch (key) {
    case Qt::Key_Space: named(kVK_Space); break;
    case Qt::Key_Tab: named(kVK_Tab); break;
    case Qt::Key_Backspace: named(kVK_Delete); break;
    case Qt::Key_Return: named(kVK_Return); break;
    case Qt::Key_Enter: named(kVK_ANSI_KeypadEnter); break;
    case Qt::Key_Escape: named(kVK_Escape); break;
    case Qt::Key_Delete: named(kVK_ForwardDelete); break;
    case Qt::Key_Home: named(kVK_Home); break;
    case Qt::Key_End: named(kVK_End); break;
    case Qt::Key_PageUp: named(kVK_PageUp); break;
    case Qt::Key_PageDown: named(kVK_PageDown); break;
    case Qt::Key_Left: named(kVK_LeftArrow); break;
    case Qt::Key_Right: named(kVK_RightArrow); break;
    case Qt::Key_Up: named(kVK_UpArrow); break;
    case Qt::Key_Down: named(kVK_DownArrow); break;
    default: break;
    }
    if (result.valid) return result;
    if (modifiers & Qt::KeypadModifier) {
        const UInt32 digits[] = {kVK_ANSI_Keypad0, kVK_ANSI_Keypad1, kVK_ANSI_Keypad2,
            kVK_ANSI_Keypad3, kVK_ANSI_Keypad4, kVK_ANSI_Keypad5, kVK_ANSI_Keypad6,
            kVK_ANSI_Keypad7, kVK_ANSI_Keypad8, kVK_ANSI_Keypad9};
        if (key >= '0' && key <= '9') named(digits[key - '0']);
        else if (key == Qt::Key_Plus) named(kVK_ANSI_KeypadPlus);
        else if (key == Qt::Key_Minus) named(kVK_ANSI_KeypadMinus);
        else if (key == Qt::Key_Asterisk) named(kVK_ANSI_KeypadMultiply);
        else if (key == Qt::Key_Slash) named(kVK_ANSI_KeypadDivide);
        else if (key == Qt::Key_Period) named(kVK_ANSI_KeypadDecimal);
        return result;
    }
    if (key <= 0 || key >= 0x10000) return result;
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef data = source ? static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData)) : nullptr;
    if (!data) {
        if (source) CFRelease(source);
        source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
        data = source ? static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData)) : nullptr;
    }
    if (data) {
        const auto *layout = reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data));
        for (UInt32 level = 0; level < 2 && !result.valid; ++level) {
            for (UInt16 code = 0; code < 128; ++code) {
                UInt32 dead = 0;
                UniChar characters[4]; UniCharCount length = 0;
                if (UCKeyTranslate(layout, code, kUCKeyActionDown, level ? (shiftKey >> 8) : 0,
                    LMGetKbdType(), kUCKeyTranslateNoDeadKeysBit, &dead, 4, &length, characters) == noErr
                    && length == 1 && QChar(characters[0]).toUpper().unicode() == key) {
                    named(code);
                    if (level) result.modifiers |= shiftKey;
                    break;
                }
            }
        }
    }
    if (source) CFRelease(source);
    return result;
}
bool same(const Hotkey &a, const Hotkey &b) { return a.valid && b.valid && a.key == b.key && a.modifiers == b.modifiers; }
bool validButton(int button) { return button == 0 || button == 2 || button == 4 || button == 5; }
QImage fromCGImage(CGImageRef image)
{
    if (!image) return {};
    const size_t width = CGImageGetWidth(image), height = CGImageGetHeight(image);
    if (!width || !height || width > 16384 || height > 16384 || width * height > 64 * 1024 * 1024) return {};
    QImage result(int(width), int(height), QImage::Format_RGBA8888_Premultiplied);
    if (result.isNull()) return {};
    result.fill(Qt::transparent);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(result.bits(), width, height, 8, result.bytesPerLine(),
        space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (space) CGColorSpaceRelease(space);
    if (!context) return {};
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    return result;
}
struct CapturedDisplay { QRectF frame; QImage image; };
QImage compose(const QVector<CapturedDisplay> &displays)
{
    if (displays.isEmpty()) return {};
    QRectF bounds;
    qreal scale = 1.0;
    for (const auto &display : displays) {
        if (display.image.isNull() || display.frame.isEmpty()) return {};
        bounds = bounds.united(display.frame);
        scale = std::max(scale, display.image.width() / display.frame.width());
    }
    scale = std::min(scale, 16384.0 / std::max(bounds.width(), bounds.height()));
    scale = std::min(scale, std::sqrt((64.0 * 1024 * 1024) / (bounds.width() * bounds.height())));
    const QSize size(int(std::ceil(bounds.width() * scale)), int(std::ceil(bounds.height() * scale)));
    QImage result(size, QImage::Format_RGB32);
    if (result.isNull()) return {};
    result.fill(Qt::black);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto &display : displays) {
        const QRectF target((display.frame.topLeft() - bounds.topLeft()) * scale, display.frame.size() * scale);
        painter.drawImage(target, display.image);
    }
    return result;
}
}

struct DesktopIntegration::Native {
    DesktopIntegration *owner;
    EventHandlerRef handler = nullptr;
    EventHotKeyRef capture = nullptr, clear = nullptr, show = nullptr;
    CFMachPortRef mouse = nullptr;
    CFRunLoopSourceRef mouseSource = nullptr;
    bool held[3] = {false, false, false};
    explicit Native(DesktopIntegration *owner) : owner(owner) {}
    static OSStatus hotkey(EventHandlerCallRef, EventRef event, void *data)
    {
        auto *state = static_cast<Native *>(data);
        EventHotKeyID id{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr
            || id.signature != hotkeySignature || id.id < 1 || id.id > 3) return eventNotHandledErr;
        if (GetEventKind(event) == kEventHotKeyReleased) { state->held[id.id - 1] = false; return noErr; }
        if (state->held[id.id - 1]) return noErr;
        state->held[id.id - 1] = true;
        if (id.id == 1 && state->capture) emit state->owner->captureRequested();
        if (id.id == 2 && state->clear) emit state->owner->clearRequested();
        if (id.id == 3 && state->show) emit state->owner->showRequested();
        return noErr;
    }
    static CGEventRef mouseEvent(CGEventTapProxy, CGEventType type, CGEventRef event, void *data)
    {
        auto *state = static_cast<Native *>(data);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            if (state->mouse) CGEventTapEnable(state->mouse, true);
            return event;
        }
        if (type != kCGEventOtherMouseDown && type != kCGEventOtherMouseUp) return event;
        const int nativeButton = int(CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber));
        const int button = nativeButton == 2 ? 2 : (nativeButton == 3 ? 4 : (nativeButton == 4 ? 5 : 0));
        auto *owner = state->owner;
        if (!button || (button != owner->m_state.captureMouse && button != owner->m_state.clearMouse)) return event;
        if (type == kCGEventOtherMouseDown) {
            const bool capture = button == owner->m_state.captureMouse;
            const auto generation = owner->m_bindingGeneration;
            QTimer::singleShot(0, owner, [owner, capture, generation] {
                if (generation != owner->m_bindingGeneration) return;
                if (capture) emit owner->captureRequested(); else emit owner->clearRequested();
            });
        }
        return nullptr; // Preserve the original button's normal action when unbound.
    }
    void removeMouse()
    {
        if (mouse) CGEventTapEnable(mouse, false);
        if (mouseSource) { CFRunLoopRemoveSource(CFRunLoopGetMain(), mouseSource, kCFRunLoopCommonModes); CFRelease(mouseSource); mouseSource = nullptr; }
        if (mouse) { CFMachPortInvalidate(mouse); CFRelease(mouse); mouse = nullptr; }
    }
    ~Native()
    {
        removeMouse();
        if (capture) UnregisterEventHotKey(capture);
        if (clear) UnregisterEventHotKey(clear);
        if (show) UnregisterEventHotKey(show);
        if (handler) RemoveEventHandler(handler);
    }
};

DesktopIntegration::DesktopIntegration(QObject *parent) : QObject(parent), m_native(std::make_unique<Native>(this))
{
    const EventTypeSpec events[] = {{kEventClassKeyboard, kEventHotKeyPressed}, {kEventClassKeyboard, kEventHotKeyReleased}};
    InstallEventHandler(GetApplicationEventTarget(), Native::hotkey, 2, events, m_native.get(), &m_native->handler);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(20000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        cancelCapture(); emit failed(tr("macOS не вернула снимок за 20 секунд. Проверьте разрешение записи экрана."));
    });
}
DesktopIntegration::~DesktopIntegration() { cancelCapture(); }

bool DesktopIntegration::registerShowShortcut(QString *error)
{
    if (error) error->clear();
    if (m_native->show) return true;
    const auto key = translate(QKeySequence(QStringLiteral("Ctrl+Alt+M")));
    if (!m_native->handler || !key.valid || RegisterEventHotKey(key.key, key.modifiers,
        EventHotKeyID{hotkeySignature, 3}, GetApplicationEventTarget(), 0, &m_native->show) != noErr) {
        if (error) *error = tr("Не удалось назначить Command+Option+M. Освободите это сочетание и повторите попытку.");
        return false;
    }
    return true;
}
bool DesktopIntegration::apply(const State &state, QString *error)
{
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    const auto capture = translate(state.capture), clear = translate(state.clear);
    const auto restore = translate(QKeySequence(QStringLiteral("Ctrl+Alt+M")));
    if (!capture.valid || !clear.valid || same(capture, clear))
        return fail(tr("Выберите два разных сочетания клавиш, поддерживаемых macOS."));
    if (same(capture, restore) || same(clear, restore))
        return fail(tr("Command+Option+M используется для возвращения окна m1zuki."));
    if (!validButton(state.captureMouse) || !validButton(state.clearMouse)
        || (state.captureMouse && state.captureMouse == state.clearMouse))
        return fail(tr("Выберите разные кнопки мыши: среднюю, Mouse4 или Mouse5."));
    if (!m_native->handler) return fail(tr("Не удалось создать обработчик горячих клавиш macOS."));
    bool newMouse = false;
    if ((state.captureMouse || state.clearMouse) && !m_native->mouse) {
        if (!AXIsProcessTrusted())
            return fail(tr("Для кнопок мыши разрешите m1zuki доступ в Системных настройках → Конфиденциальность и безопасность → Универсальный доступ, затем повторите сохранение."));
        m_native->mouse = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
            CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp), Native::mouseEvent, m_native.get());
        if (!m_native->mouse) return fail(tr("macOS не разрешила глобальные кнопки мыши. Проверьте разрешения Универсального доступа и Мониторинга ввода."));
        m_native->mouseSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_native->mouse, 0);
        if (!m_native->mouseSource) { m_native->removeMouse(); return fail(tr("Не удалось подключить обработчик кнопок мыши.")); }
        CFRunLoopAddSource(CFRunLoopGetMain(), m_native->mouseSource, kCFRunLoopCommonModes);
        CGEventTapEnable(m_native->mouse, true);
        newMouse = true;
    }
    const auto registerKey = [](const Hotkey &key, UInt32 id, EventHotKeyRef *target) {
        return RegisterEventHotKey(key.key, key.modifiers, EventHotKeyID{hotkeySignature, id}, GetApplicationEventTarget(), 0, target) == noErr;
    };
    if (m_native->capture) { UnregisterEventHotKey(m_native->capture); m_native->capture = nullptr; }
    if (m_native->clear) { UnregisterEventHotKey(m_native->clear); m_native->clear = nullptr; }
    std::fill(std::begin(m_native->held), std::end(m_native->held), false);
    if (!registerKey(capture, 1, &m_native->capture) || !registerKey(clear, 2, &m_native->clear)) {
        if (m_native->capture) { UnregisterEventHotKey(m_native->capture); m_native->capture = nullptr; }
        if (m_native->clear) { UnregisterEventHotKey(m_native->clear); m_native->clear = nullptr; }
        const auto oldCapture = translate(m_state.capture), oldClear = translate(m_state.clear);
        bool restored = true;
        if (oldCapture.valid) restored &= registerKey(oldCapture, 1, &m_native->capture);
        if (oldClear.valid) restored &= registerKey(oldClear, 2, &m_native->clear);
        if (newMouse) m_native->removeMouse();
        return fail(tr("Горячая клавиша занята или зарезервирована macOS.%1")
            .arg(restored ? QString{} : tr(" Прежние сочетания восстановить не удалось.")));
    }
    if (!state.captureMouse && !state.clearMouse) m_native->removeMouse();
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
        if (m_native->capture) { UnregisterEventHotKey(m_native->capture); m_native->capture = nullptr; }
        if (m_native->clear) { UnregisterEventHotKey(m_native->clear); m_native->clear = nullptr; }
        m_native->removeMouse();
        m_state = *m_previous;
        ++m_bindingGeneration;
    } else if (!apply(*m_previous, error)) return false;
    m_previous.reset();
    return true;
}

void DesktopIntegration::finishCapture(unsigned generation, const QImage &image, const QString &error)
{
    if (!m_capturing || generation != m_generation) return;
    m_timeout.stop();
    m_capturing = false;
    if (!error.isEmpty()) emit failed(error);
    else if (image.isNull()) emit failed(tr("macOS не вернула изображение экрана."));
    else emit captured(image);
}

void DesktopIntegration::captureScreen(bool currentMonitor)
{
    if (m_capturing) { emit failed(tr("Предыдущий снимок экрана ещё создаётся.")); return; }
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        emit failed(tr("Разрешите m1zuki запись экрана в Системных настройках → Конфиденциальность и безопасность. После изменения разрешения перезапустите приложение."));
        return;
    }
    m_capturing = true;
    const auto generation = ++m_generation;
    m_timeout.start();
    QPointer<DesktopIntegration> owner(this);
    if (@available(macOS 14.0, *)) {
        CGEventRef cursorEvent = CGEventCreate(nullptr);
        const CGPoint cursor = cursorEvent ? CGEventGetLocation(cursorEvent) : CGPointZero;
        if (cursorEvent) CFRelease(cursorEvent);
        [SCShareableContent getShareableContentExcludingDesktopWindows:NO onScreenWindowsOnly:YES
            completionHandler:^(SCShareableContent *content, NSError *error) {
            QTimer::singleShot(0, qApp, [owner, generation, currentMonitor, cursor, content, error] {
                if (!owner || !owner->m_capturing || owner->m_generation != generation) return;
                if (error || !content || content.displays.count == 0) {
                    owner->finishCapture(generation, {}, tr("Не удалось получить список экранов macOS. Проверьте разрешение записи экрана.")); return;
                }
                NSMutableArray<SCDisplay *> *selected = [NSMutableArray array];
                for (SCDisplay *display in content.displays)
                    if (!currentMonitor || CGRectContainsPoint(display.frame, cursor)) [selected addObject:display];
                if (selected.count == 0) {
                    for (SCDisplay *display in content.displays)
                        if (display.displayID == CGMainDisplayID()) { [selected addObject:display]; break; }
                    if (selected.count == 0) [selected addObject:content.displays.firstObject];
                }
                NSMutableArray<SCRunningApplication *> *excluded = [NSMutableArray array];
                for (SCRunningApplication *application in content.applications)
                    if (application.processID == NSProcessInfo.processInfo.processIdentifier) [excluded addObject:application];
                struct Batch { QVector<CapturedDisplay> images; int remaining = 0; };
                auto batch = std::make_shared<Batch>();
                batch->images.resize(int(selected.count)); batch->remaining = int(selected.count);
                for (NSUInteger index = 0; index < selected.count; ++index) {
                    SCDisplay *display = selected[index];
                    batch->images[int(index)].frame = QRectF(display.frame.origin.x, display.frame.origin.y,
                        display.frame.size.width, display.frame.size.height);
                    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingApplications:excluded exceptingWindows:@[]];
                    SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
                    configuration.width = CGDisplayPixelsWide(display.displayID);
                    configuration.height = CGDisplayPixelsHigh(display.displayID);
                    configuration.showsCursor = NO;
                    configuration.capturesAudio = NO;
                    [SCScreenshotManager captureImageWithFilter:filter configuration:configuration
                        completionHandler:^(CGImageRef image, NSError *captureError) {
                        const QImage converted = captureError ? QImage{} : fromCGImage(image);
                        QTimer::singleShot(0, qApp, [owner, generation, batch, index, converted] {
                            if (!owner || !owner->m_capturing || owner->m_generation != generation) return;
                            if (converted.isNull()) {
                                owner->finishCapture(generation, {}, tr("Не удалось снять экран macOS. Проверьте доступ к записи экрана.")); return;
                            }
                            batch->images[int(index)].image = converted;
                            if (--batch->remaining == 0) owner->finishCapture(generation, compose(batch->images), {});
                        });
                    }];
                }
            });
        }];
    } else {
        // Qt's native screen grab is retained solely for macOS 13. Newer
        // systems use ScreenCaptureKit instead of deprecated capture APIs.
        QTimer::singleShot(0, this, [owner, generation, currentMonitor] {
            if (!owner || !owner->m_capturing || owner->m_generation != generation) return;
            auto screens = QGuiApplication::screens();
            if (currentMonitor) {
                auto *screen = QGuiApplication::screenAt(QCursor::pos());
                if (!screen) screen = QGuiApplication::primaryScreen();
                screens = screen ? QList<QScreen *>{screen} : QList<QScreen *>{};
            }
            QVector<CapturedDisplay> images;
            for (auto *screen : screens) images.append({QRectF(screen->geometry()), screen->grabWindow(0).toImage()});
            owner->finishCapture(generation, compose(images), {});
        });
    }
}
void DesktopIntegration::cancelCapture()
{
    ++m_generation;
    m_capturing = false;
    m_timeout.stop();
}
