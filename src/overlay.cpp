#include "overlay.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWindow>
#include <algorithm>

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
        if (!m_numbers.isEmpty() && !layoutNumbers()) {
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
    if (numbers.isEmpty() || std::any_of(numbers.begin(), numbers.end(),
                                       [](int number) { return number < 1 || number > 99; })) {
        clear();
        return;
    }
    m_numbers = numbers;
    std::sort(m_numbers.begin(), m_numbers.end());
    m_numbers.erase(std::unique(m_numbers.begin(), m_numbers.end()), m_numbers.end());
    m_fontPixels = std::clamp(fontPixels, 12, 96);
    if (!layoutNumbers()) {
        clear();
        return;
    }
    QStringList labels;
    for (int number : m_numbers)
        labels.append(QString::number(number));
    setAccessibleName(labels.join(QStringLiteral(", ")));
    positionOverlay();
    show();
    update();
    if (seconds > 0)
        m_hideTimer.start(std::min(seconds, 3600) * 1000);
    else
        m_hideTimer.stop();
}

bool Overlay::layoutNumbers()
{
    const auto *screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->geometry().size() - QSize(24, 24) : QSize(776, 576);
    if (available.width() <= 8 || available.height() <= 8)
        return false;
    QFont numberFont;
    numberFont.setFamilies({QStringLiteral("Adwaita Sans"), QStringLiteral("Inter"),
                           QStringLiteral("Fira Sans"), QStringLiteral("sans-serif")});
    numberFont.setWeight(QFont::DemiBold);
    numberFont.setStyleStrategy(QFont::PreferAntialias);
    // Keep the chosen size for ordinary answers; wrap only between options.
    // Very long sets shrink only if the wrapped text would exceed the screen.
    for (int pixels = m_fontPixels; pixels >= 12; --pixels) {
        numberFont.setPixelSize(pixels);
        const QFontMetrics metrics(numberFont);
        QStringList lines;
        QString line;
        for (qsizetype i = 0; i < m_numbers.size(); ++i) {
            QString token = QString::number(m_numbers[i]);
            if (i + 1 < m_numbers.size()) token += QLatin1Char(',');
            const QString candidate = line.isEmpty() ? token : line + QLatin1Char(' ') + token;
            if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > available.width() - 8) {
                lines.append(line);
                line = token;
            } else {
                line = candidate;
            }
        }
        if (!line.isEmpty()) lines.append(line);
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

void Overlay::clear()
{
    m_hideTimer.stop();
    hide();
    m_numbers.clear();
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
