#include "overlay.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

Overlay::Overlay(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
    setObjectName(QStringLiteral("answerOverlay"));
    setWindowTitle(QStringLiteral("m1zuki overlay"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &Overlay::clear);

    // Create the native QWindow while still hidden so LayerShellQt can select
    // its Wayland surface role before the first show().
    winId();
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QLatin1String("windows")) {
        const auto hwnd = reinterpret_cast<HWND>(winId());
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE)
                          | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
    }
#endif
#ifdef HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setScope(QStringLiteral("minianswer-overlay"));
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                          | LayerShellQt::Window::AnchorLeft);
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setExclusiveZone(-1);
        layer->setMargins(QMargins(12, 12, 0, 0));
    }
#endif
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] {
        if (!m_sourceLines.isEmpty() && !layoutTextLines()) {
            clear();
            return;
        }
        positionOverlay();
    });
}

void Overlay::showNumber(int number, int seconds, int fontPixels)
{
    showNumbers({number}, seconds, fontPixels);
}

void Overlay::showNumbers(const QList<int> &numbers, int seconds, int fontPixels)
{
    m_error = false;
    if (numbers.isEmpty() || std::any_of(numbers.begin(), numbers.end(),
                                       [](int number) { return number < 1 || number > 99; })) {
        clear();
        return;
    }
    m_numbers = numbers;
    std::sort(m_numbers.begin(), m_numbers.end());
    m_numbers.erase(std::unique(m_numbers.begin(), m_numbers.end()), m_numbers.end());
    QStringList labels;
    for (int number : m_numbers)
        labels.append(QString::number(number));
    m_sourceLines = {labels.join(QStringLiteral(", "))};
    m_fontPixels = std::clamp(fontPixels, 12, 96);
    if (!layoutTextLines()) {
        clear();
        return;
    }
    setAccessibleName(labels.join(QStringLiteral(", ")));
    positionOverlay();
    show();
    update();
    if (seconds > 0)
        m_hideTimer.start(std::min(seconds, 3600) * 1000);
    else
        m_hideTimer.stop();
}

void Overlay::showQuestionAnswers(const QList<QList<int>> &answers, int seconds, int fontPixels)
{
    m_error = false;
    if (answers.isEmpty()) {
        clear();
        return;
    }

    m_numbers.clear();
    m_sourceLines.clear();
    bool hasAnswer = false;
    for (qsizetype i = 0; i < answers.size(); ++i) {
        QList<int> choices = answers[i];
        if (std::any_of(choices.begin(), choices.end(),
                        [](int number) { return number < 1 || number > 99; })) {
            clear();
            return;
        }
        std::sort(choices.begin(), choices.end());
        choices.erase(std::unique(choices.begin(), choices.end()), choices.end());
        QStringList labels;
        for (const int choice : choices) {
            labels.append(QString::number(choice));
            m_numbers.append(choice);
        }
        hasAnswer = hasAnswer || !choices.isEmpty();
        m_sourceLines.append(QStringLiteral("Q%1: %2")
                                 .arg(i + 1)
                                 .arg(labels.isEmpty() ? QStringLiteral("?")
                                                       : labels.join(QStringLiteral(", "))));
    }
    if (!hasAnswer) {
        clear();
        return;
    }

    m_fontPixels = std::clamp(fontPixels, 12, 96);
    if (!layoutTextLines()) {
        clear();
        return;
    }
    setAccessibleName(m_sourceLines.join(QStringLiteral(" | ")));
    positionOverlay();
    show();
    update();
    if (seconds > 0)
        m_hideTimer.start(std::min(seconds, 3600) * 1000);
    else
        m_hideTimer.stop();
}

bool Overlay::layoutTextLines()
{
    const auto *screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->geometry().size() - QSize(24, 24) : QSize(776, 576);
    if (available.width() <= 8 || available.height() <= 8)
        return false;
    QFont numberFont;
    numberFont.setFamilies({QStringLiteral("Adwaita Sans"), QStringLiteral("Inter"),
                           QStringLiteral("Fira Sans"), QStringLiteral("Segoe UI"), QStringLiteral("sans-serif")});
    numberFont.setWeight(QFont::DemiBold);
    numberFont.setStyleStrategy(QFont::PreferAntialias);
    for (int pixels = m_fontPixels; pixels >= 12; --pixels) {
        numberFont.setPixelSize(pixels);
        const QFontMetrics metrics(numberFont);
        QStringList lines;
        for (const auto &sourceLine : m_sourceLines) {
            QString line;
            const auto tokens = sourceLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const auto &token : tokens) {
                const QString candidate = line.isEmpty() ? token : line + QLatin1Char(' ') + token;
                if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > available.width() - 8) {
                    lines.append(line);
                    line = token;
                } else {
                    line = candidate;
                }
            }
            if (!line.isEmpty())
                lines.append(line);
        }
        int width = 0;
        for (const auto &text : lines)
            width = std::max(width, metrics.horizontalAdvance(text));
        const int height = metrics.height() + (lines.size() - 1) * metrics.lineSpacing() + 8;
        if (width + 8 <= available.width() && height <= available.height()) {
            m_lines = lines;
            setFont(numberFont);
            setFixedSize(width + 8, height);
            return true;
        }
    }
    return false;
}

void Overlay::showError(int seconds)
{
    clear();
    m_error = true;
    setFixedSize(24, 24);
    setAccessibleName(tr("Ошибка"));
    positionOverlay();
    show();
    update();
    m_hideTimer.start(std::clamp(seconds, 1, 300) * 1000);
}

void Overlay::clear()
{
    m_error = false;
    m_hideTimer.stop();
    hide();
    m_numbers.clear();
    m_sourceLines.clear();
    m_lines.clear();
    setAccessibleName({});
}

void Overlay::positionOverlay()
{
    auto *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
#ifdef HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setScreen(screen);
        return;
    }
#endif
    move(screen->geometry().topLeft() + QPoint(12, 12));
}

void Overlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_error) {
        QPainterPath cross;
        cross.moveTo(7, 7); cross.lineTo(17, 17);
        cross.moveTo(17, 7); cross.lineTo(7, 17);
        painter.strokePath(cross, QPen(QColor(0, 0, 0, 180), 4.5, Qt::SolidLine, Qt::RoundCap));
        painter.strokePath(cross, QPen(QColor("#f4b6c3"), 2.0, Qt::SolidLine, Qt::RoundCap));
        return;
    }
    QPainterPath outline;
    const QFontMetrics metrics(font());
    for (qsizetype i = 0; i < m_lines.size(); ++i)
        outline.addText(QPointF(4, metrics.ascent() + 4 + i * metrics.lineSpacing()), font(), m_lines[i]);
    // A soft shadow and fine edge keep the lighter type readable on both
    // bright pages and dark video, without the previous heavy black stroke.
    painter.save();
    painter.translate(0, 1);
    painter.strokePath(outline, QPen(QColor(0, 0, 0, 24), 4.0,
                                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.strokePath(outline, QPen(QColor(0, 0, 0, 40), 2.5,
                                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.restore();
    painter.strokePath(outline, QPen(QColor(0, 0, 0, 165), 1.3,
                                   Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.fillPath(outline, Qt::white);
}
