// Manual desktop integration probe. No screenshots or network requests here.
// Run three separate processes in sequence over a --background process:
// --unprotected is the positive visibility control; --protected tests KWin.
#include "captureprotection.h"
#include "overlay.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <QWindow>
#include <cstdio>
#include <unistd.h>

#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace {
void printEvent(const char *event, const QJsonObject &details)
{
    const auto json = QJsonDocument(details).toJson(QJsonDocument::Compact);
    std::printf("%s %s\n", event, json.constData());
    std::fflush(stdout);
}

class Background final : public QWidget {
public:
    Background()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                              | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
    {
        setWindowTitle(QStringLiteral("MiniAnswer synthetic capture background"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(200, 160);
        winId();
#ifdef HAVE_LAYER_SHELL
        auto *layer = LayerShellQt::Window::get(windowHandle());
        layer->setScope(QStringLiteral("minianswer-privacy-probe-background"));
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop)
                          | LayerShellQt::Window::AnchorLeft);
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setExclusiveZone(-1);
        layer->setMargins(QMargins());
        layer->setScreen(QGuiApplication::primaryScreen());
#endif
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#28628c")));
    }
};
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("privacy_probe"));
    app.setDesktopFileName(QStringLiteral("privacy_probe"));
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Manual local capture-exclusion test. Displays synthetic pixels for 30 seconds; "
        "does not capture the screen or call any API."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("background"), QStringLiteral("Show a separate-process solid 200x160 background.")});
    parser.addOption({QStringLiteral("unprotected"), QStringLiteral("Show numeral 3 without capture exclusion (control).")});
    parser.addOption({QStringLiteral("protected"), QStringLiteral("Show numeral 3 with real CaptureProtection (default).")});
    parser.process(app);

    const bool backgroundMode = parser.isSet(QStringLiteral("background"));
    const bool unprotectedMode = parser.isSet(QStringLiteral("unprotected"));
    if ((backgroundMode && unprotectedMode)
        || ((backgroundMode || unprotectedMode) && parser.isSet(QStringLiteral("protected")))) {
        printEvent("FAIL", {{QStringLiteral("error"), QStringLiteral("Choose exactly one mode.")}});
        return 2;
    }
#ifndef HAVE_LAYER_SHELL
    printEvent("FAIL", {{QStringLiteral("error"), QStringLiteral("This probe requires LayerShellQt.")}});
    return 2;
#endif
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
        || !QGuiApplication::primaryScreen()) {
        printEvent("FAIL", {{QStringLiteral("error"), QStringLiteral("This probe requires an active Wayland screen.")}});
        return 2;
    }

    const QString mode = backgroundMode ? QStringLiteral("background")
        : unprotectedMode ? QStringLiteral("unprotected") : QStringLiteral("protected");
    auto ready = [&app, mode](QWidget *window) {
        // Wait for mapping and at least one compositor frame before declaring
        // readiness. The runner should still verify the KWin window state.
        QTimer::singleShot(500, &app, [window, mode] {
            const auto *screen = QGuiApplication::primaryScreen();
            const auto geometry = screen->geometry();
            const bool exposed = window->windowHandle() && window->windowHandle()->isExposed();
            printEvent(window->isVisible() && exposed ? "READY" : "FAIL", {
                {QStringLiteral("mode"), mode},
                {QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())},
                {QStringLiteral("visible"), window->isVisible()},
                {QStringLiteral("exposed"), exposed},
                {QStringLiteral("screen"), screen->name()},
                {QStringLiteral("screen_count"), QGuiApplication::screens().size()},
                {QStringLiteral("screen_width"), geometry.width()},
                {QStringLiteral("screen_height"), geometry.height()},
                {QStringLiteral("capture_x"), geometry.x()},
                {QStringLiteral("capture_y"), geometry.y()},
                {QStringLiteral("capture_width"), 200},
                {QStringLiteral("capture_height"), 160},
                {QStringLiteral("device_pixel_ratio"), screen->devicePixelRatio()},
                {QStringLiteral("background_rgb"), QStringLiteral("#28628c")}
            });
            if (!window->isVisible() || !exposed)
                QCoreApplication::exit(3);
        });
    };

    QTimer::singleShot(30000, &app, &QCoreApplication::quit);
    // The runner can request normal shutdown, allowing CaptureProtection to
    // unload its temporary KWin script instead of killing the process.
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read, &app);
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char buffer[64];
        const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0)
            input.setEnabled(false);
        else if (QByteArray(buffer, count).contains("quit"))
            app.quit();
    });
    if (backgroundMode) {
        Background background;
        background.show();
        ready(&background);
        return app.exec();
    }

    // Keep protection alive for the entire overlay lifetime. Destruction
    // removes the temporary KWin script when the process exits normally.
    CaptureProtection protection;
    Overlay overlay;
    if (unprotectedMode) {
        overlay.showNumber(3, 30, 64);
        ready(&overlay);
    } else {
        QObject::connect(&protection, &CaptureProtection::statusChanged, &app,
                         [&](bool protectedNow, const QString &message) {
            if (protectedNow) {
                overlay.showNumber(3, 30, 64);
                ready(&overlay);
            } else {
                overlay.clear();
                printEvent("STATUS", {{QStringLiteral("protected"), false},
                                      {QStringLiteral("message"), message}});
            }
        });
        QTimer::singleShot(7000, &app, [&] {
            if (!protection.isReady()) {
                printEvent("FAIL", {{QStringLiteral("error"), QStringLiteral("Capture protection did not become ready.")}});
                app.exit(4);
            }
        });
        protection.start();
    }
    return app.exec();
}
