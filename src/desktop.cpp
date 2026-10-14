#include "desktop.h"

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <QAction>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
constexpr auto componentName = "MiniAnswer";

bool shortcutAvailable(const QKeySequence &sequence)
{
    const auto conflicts = KGlobalAccel::globalShortcutsByKey(sequence);
    for (const auto &conflict : conflicts) {
        if (conflict.componentUniqueName() != QLatin1String(componentName))
            return false;
    }
    return true;
}
}

DesktopIntegration::DesktopIntegration(QObject *parent)
    : QObject(parent)
    , m_captureAction(new QAction(tr("Получить ответы"), this))
    , m_clearAction(new QAction(tr("Скрыть ответы"), this))
{
    m_captureAction->setObjectName(QStringLiteral("capture"));
    m_clearAction->setObjectName(QStringLiteral("clear"));
    for (auto *action : {m_captureAction, m_clearAction}) {
        action->setProperty("componentName", componentName);
        action->setProperty("componentDisplayName", "m1zuki");
    }
    connect(m_captureAction, &QAction::triggered, this, &DesktopIntegration::captureRequested);
    connect(m_clearAction, &QAction::triggered, this, &DesktopIntegration::clearRequested);
    m_captureTimeout.setSingleShot(true);
    m_captureTimeout.setInterval(20000);
    connect(&m_captureTimeout, &QTimer::timeout, this, [this] {
        cleanCapture();
        emit failed(tr("Не удалось получить снимок экрана за 20 секунд."));
    });
}

DesktopIntegration::~DesktopIntegration()
{
    cleanCapture();
    if (!m_shortcutsTouched)
        return;
    auto *accelerator = KGlobalAccel::self();
    if (accelerator->hasShortcut(m_captureAction))
        accelerator->removeAllShortcuts(m_captureAction);
    if (accelerator->hasShortcut(m_clearAction))
        accelerator->removeAllShortcuts(m_clearAction);
}

bool DesktopIntegration::registerShortcuts(const QKeySequence &capture,
                                           const QKeySequence &clear, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (error)
        error->clear();
    if (capture.isEmpty() || clear.isEmpty())
        return fail(tr("Укажите обе горячие клавиши."));
    if (capture.count() != 1 || clear.count() != 1)
        return fail(tr("Используйте одно сочетание клавиш для каждого действия."));
    if (capture == clear)
        return fail(tr("Горячие клавиши снимка и скрытия должны отличаться."));

    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel")))
        return fail(tr("Служба горячих клавиш KDE недоступна. Запустите приложение в сеансе KDE Plasma."));

    for (const auto &sequence : {capture, clear}) {
        if (!shortcutAvailable(sequence)) {
            return fail(tr("Сочетание %1 уже используется другой программой.")
                            .arg(sequence.toString(QKeySequence::NativeText)));
        }
    }

    auto *accelerator = KGlobalAccel::self();
    m_shortcutsTouched = true;
    const auto previousCapture = accelerator->shortcut(m_captureAction);
    const auto previousClear = accelerator->shortcut(m_clearAction);
    // Release our two old bindings first so swapping them is supported.
    accelerator->setShortcut(m_captureAction, {}, KGlobalAccel::NoAutoloading);
    accelerator->setShortcut(m_clearAction, {}, KGlobalAccel::NoAutoloading);
    const bool captureSet = accelerator->setShortcut(m_captureAction, {capture}, KGlobalAccel::NoAutoloading);
    const bool clearSet = accelerator->setShortcut(m_clearAction, {clear}, KGlobalAccel::NoAutoloading);
    if (!captureSet || !clearSet || accelerator->shortcut(m_captureAction) != QList<QKeySequence>{capture}
        || accelerator->shortcut(m_clearAction) != QList<QKeySequence>{clear}) {
        accelerator->setShortcut(m_captureAction, {}, KGlobalAccel::NoAutoloading);
        accelerator->setShortcut(m_clearAction, {}, KGlobalAccel::NoAutoloading);
        accelerator->setShortcut(m_captureAction, previousCapture, KGlobalAccel::NoAutoloading);
        accelerator->setShortcut(m_clearAction, previousClear, KGlobalAccel::NoAutoloading);
        return fail(tr("KDE не удалось назначить горячие клавиши."));
    }
    return true;
}

void DesktopIntegration::captureScreen(bool currentMonitor)
{
    if (m_process) {
        emit failed(tr("Предыдущий снимок экрана ещё создаётся."));
        return;
    }
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (executable.isEmpty()) {
        emit failed(tr("Установите Spectacle: он нужен для снимков экрана в KDE, включая Wayland."));
        return;
    }
    m_captureDirectory = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/minianswer-XXXXXX"));
    if (!m_captureDirectory->isValid()) {
        cleanCapture();
        emit failed(tr("Не удалось создать временную папку для снимка экрана."));
        return;
    }
    m_capturePath = m_captureDirectory->filePath(QStringLiteral("screen.png"));
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        finishCapture(status == QProcess::NormalExit ? exitCode : -1);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            cleanCapture();
            emit failed(tr("Не удалось запустить Spectacle."));
        }
    });
    m_captureTimeout.start();
    // An independent instance prevents a running Spectacle GUI from handling
    // the request after this process has exited (and the temp directory is gone).
    m_process->start(executable, {QStringLiteral("-i"), QStringLiteral("-b"), QStringLiteral("-n"),
                                 currentMonitor ? QStringLiteral("-m") : QStringLiteral("-f"),
                                 QStringLiteral("-o"), m_capturePath});
}

void DesktopIntegration::finishCapture(int exitCode)
{
    QImage image;
    if (exitCode == 0)
        image.load(m_capturePath);
    cleanCapture();
    if (image.isNull()) {
        emit failed(tr("Spectacle не вернул снимок экрана. Проверьте доступ к экрану и повторите попытку."));
    } else {
        emit captured(image);
    }
}

void DesktopIntegration::cancelCapture()
{
    cleanCapture();
}

void DesktopIntegration::cleanCapture()
{
    m_captureTimeout.stop();
    if (m_process) {
        auto *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1500);
        }
        process->deleteLater();
    }
    m_capturePath.clear();
    m_captureDirectory.reset();
}
