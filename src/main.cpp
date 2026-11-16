#include "controller.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
#if !defined(HAVE_LAYER_SHELL) && defined(Q_OS_LINUX)
    // XWayland fallback when native Wayland layer positioning is unavailable.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QApplication app(argc, argv);
    app.setApplicationName("m1zuki");
    app.setApplicationDisplayName("m1zuki");
    app.setOrganizationName("MiniAnswer");
    app.setDesktopFileName("m1zuki");
    app.setApplicationVersion("1.4.0");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.setApplicationDescription("mizukimizukimizukimizuki");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"capture", "Снять экран и получить ответ."});
    parser.addOption({"clear", "Скрыть ответ и отменить текущий запрос."});
    parser.addOption({"settings", "Открыть настройки."});
    parser.addOption({"quit", "Завершить запущенное приложение."});
    parser.addOption({"demo", "Показать номера 1, 3, 4 без снимка экрана и API-запроса."});
    parser.addOption({"render-preview", "Сохранить изображение окна настроек без подключения к рабочему столу.", "path"});
    parser.process(app);
    if (parser.isSet("render-preview")) {
        Controller controller(true);
        return controller.renderPreview(parser.value("render-preview")) ? 0 : 1;
    }
    QString command = "settings";
    for (const auto &option : {"capture", "clear", "quit", "demo"}) if (parser.isSet(option)) command = option;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(runtime)) return 1;
#ifdef Q_OS_MACOS
    if (!QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
#endif
#else
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
#endif
    if (runtime.isEmpty()) { std::fprintf(stderr, "Нет защищённого каталога XDG_RUNTIME_DIR.\n"); return 1; }
    const QString socketPath = QDir(runtime).filePath("mini-answer.sock");
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (socket.waitForConnected(400)) {
        socket.write(command.toUtf8() + '\n');
        socket.flush();
        socket.waitForBytesWritten(1000);
        return 0;
    }
    if (command == "quit" || command == "clear") return 0;
    QLockFile instanceLock(QDir(runtime).filePath("mini-answer.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock()) {
        std::fprintf(stderr, "m1zuki уже запускается. Повторите команду через секунду.\n");
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    // Recover only a stale socket, never a healthy instance.
    QLocalServer::removeServer(socketPath);
    if (!server.listen(socketPath)) { std::fprintf(stderr, "Не удалось создать локальный канал приложения.\n"); return 1; }
    Controller controller;
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto *connection = server.nextPendingConnection()) {
            auto consume = [connection, &controller] {
                if (connection->bytesAvailable() > 128) { connection->disconnectFromServer(); return; }
                if (!connection->canReadLine()) return;
                const QString message = QString::fromUtf8(connection->readLine(128)).trimmed();
                if (QStringList{"settings", "capture", "clear", "demo", "quit"}.contains(message)) controller.handleCommand(message);
                connection->disconnectFromServer();
            };
            QObject::connect(connection, &QLocalSocket::readyRead, &app, consume);
            QObject::connect(connection, &QLocalSocket::disconnected, connection, &QObject::deleteLater);
            QTimer::singleShot(2000, connection, [connection] { connection->disconnectFromServer(); });
            consume();
        }
    });
    QTimer::singleShot(0, &app, [&] { controller.handleCommand(command); });
    return app.exec();
}
