#include "captureprotection.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>

namespace {
constexpr auto kwinService = "org.kde.KWin";
constexpr auto scriptingPath = "/Scripting";
constexpr auto scriptingInterface = "org.kde.kwin.Scripting";
constexpr auto callbackPath = "/MiniAnswer/CaptureProtection";
constexpr auto callbackInterface = "org.minianswer.CaptureProtection";

QString jsString(const QString &text)
{
    // JSON quoting handles paths, tokens and executable names without allowing
    // any of them to change the generated JavaScript program.
    const auto array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}

QDBusMessage scriptCall(const QString &owner, const QString &method)
{
    return QDBusMessage::createMethodCall(owner, QLatin1String(scriptingPath),
                                        QLatin1String(scriptingInterface), method);
}
}

CaptureProtection::CaptureProtection(QObject *parent)
    : QObject(parent)
    , m_serviceWatcher(new QDBusServiceWatcher(QLatin1String(kwinService),
          QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this))
{
    m_startupTimeout.setSingleShot(true);
    m_startupTimeout.setInterval(5000);
    connect(&m_startupTimeout, &QTimer::timeout, this, [this] {
        fail(tr("KWin не подтвердил скрытие из записи экрана за 5 секунд. Оверлей отключён."));
    });
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &CaptureProtection::ownerChanged);
}

CaptureProtection::~CaptureProtection()
{
    m_startupTimeout.stop();
    // Address the unique owner, never a replacement compositor with reused ids.
    unloadScript();
    if (m_objectRegistered)
        QDBusConnection::sessionBus().unregisterObject(QLatin1String(callbackPath));
}

bool CaptureProtection::isReady() const
{
    return m_ready;
}

void CaptureProtection::start()
{
    m_requested = true;
    if (m_ready || m_starting)
        return;
    m_failed = false;
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        fail(tr("Скрытие из записи экрана поддерживается в KDE Wayland. Оверлей отключён."));
        return;
    }

    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()) {
        fail(tr("Шина сеанса D-Bus недоступна. Оверлей отключён."));
        return;
    }
    const QDBusReply<QString> owner = bus.interface()->serviceOwner(QLatin1String(kwinService));
    if (!owner.isValid() || owner.value().isEmpty()) {
        fail(tr("KWin недоступен. Оверлей отключён до восстановления защиты записи экрана."));
        return;
    }
    unloadScript();
    m_kwinOwner = owner.value();

    if (!m_objectRegistered) {
        m_objectRegistered = bus.registerObject(QLatin1String(callbackPath), this,
                                               QDBusConnection::ExportScriptableSlots);
        if (!m_objectRegistered) {
            fail(tr("Не удалось создать канал подтверждения защиты экрана. Оверлей отключён."));
            return;
        }
    }

    m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pluginName = QStringLiteral("minianswer-capture-%1-%2")
                       .arg(QCoreApplication::applicationPid()).arg(m_token);
    m_scriptDirectory = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/minianswer-capture-XXXXXX"));
    if (!m_scriptDirectory->isValid()) {
        fail(tr("Не удалось создать временный модуль защиты экрана. Оверлей отключён."));
        return;
    }

    const QString script = QStringLiteral(R"JS(
(function () {
    "use strict";
    const targetPid = %1;
    const targetClass = %2;
    const service = %3;
    const token = %4;
    let failed = false;
    function report(ok) {
        if (!ok) failed = true;
        callDBus(service, "/MiniAnswer/CaptureProtection", "org.minianswer.CaptureProtection",
                 "report", token, ok && !failed);
    }
    function ours(window) {
        return Number(window.pid) === targetPid && String(window.resourceClass) === targetClass;
    }
    function protect(window) {
        if (!ours(window)) return;
        try {
            if (typeof window.excludeFromCapture !== "boolean") { report(false); return; }
            window.excludeFromCapture = true;
            if (window.excludeFromCapture !== true) { report(false); return; }
            window.excludeFromCaptureChanged.connect(function () {
                if (window.excludeFromCapture === true) return;
                try {
                    window.excludeFromCapture = true;
                    if (window.excludeFromCapture !== true) report(false);
                } catch (error) { report(false); }
            });
        } catch (error) { report(false); }
    }
    try {
        // KWin emits this synchronously while adding the surface, before its
        // next scene render. The filter is set before any numeral is painted.
        workspace.windowAdded.connect(protect);
        const windows = workspace.windowList();
        let supported = false;
        for (let i = 0; i < windows.length; ++i) {
            if (typeof windows[i].excludeFromCapture === "boolean") supported = true;
            protect(windows[i]);
        }
        report(supported && !failed);
    } catch (error) { report(false); }
})();
)JS").arg(QString::number(QCoreApplication::applicationPid()),
          jsString(QFileInfo(QCoreApplication::applicationFilePath()).fileName()),
          jsString(bus.baseService()), jsString(m_token));

    QFile file(m_scriptDirectory->filePath(QStringLiteral("protect.js")));
    const QByteArray bytes = script.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size()) {
        file.close();
        fail(tr("Не удалось записать временный модуль защиты экрана. Оверлей отключён."));
        return;
    }
    file.close();
    m_starting = true;
    m_startupTimeout.start();
    emit statusChanged(false, tr("KWin: включаю скрытие приложения из записи экрана…"));
    auto call = scriptCall(m_kwinOwner, QStringLiteral("loadScript"));
    call << file.fileName() << m_pluginName;
    const QString token = m_token;
    auto *load = new QDBusPendingCallWatcher(bus.asyncCall(call, 5000), this);
    connect(load, &QDBusPendingCallWatcher::finished, this, [this, token](QDBusPendingCallWatcher *watcher) {
        const QDBusPendingReply<int> reply = *watcher;
        watcher->deleteLater();
        if (token != m_token || m_failed)
            return;
        if (reply.isError() || reply.value() < 0) {
            fail(tr("KWin не загрузил защиту записи экрана. Оверлей отключён."));
            return;
        }
        auto run = QDBusMessage::createMethodCall(
            m_kwinOwner, QStringLiteral("/Scripting/Script%1").arg(reply.value()),
            QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
        auto *running = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(run, 5000), this);
        connect(running, &QDBusPendingCallWatcher::finished, this,
                [this, token](QDBusPendingCallWatcher *done) {
            const QDBusPendingReply<> result = *done;
            done->deleteLater();
            if (token == m_token && result.isError())
                fail(tr("KWin не запустил защиту записи экрана. Оверлей отключён."));
        });
    });
}

void CaptureProtection::report(const QString &token, bool ready)
{
    if (!calledFromDBus() || message().service() != m_kwinOwner || token != m_token || m_failed)
        return;
    if (!ready) {
        fail(tr("KWin не подтвердил исключение окон из записи экрана. Оверлей отключён."));
        return;
    }
    if (!m_starting && !m_ready)
        return;
    m_startupTimeout.stop();
    m_starting = false;
    if (!m_ready) {
        m_ready = true;
        emit statusChanged(true, tr("KWin: приложение скрыто из записи экрана."));
    }
}

void CaptureProtection::fail(const QString &message)
{
    m_startupTimeout.stop();
    m_starting = false;
    m_ready = false;
    m_failed = true;
    emit statusChanged(false, message);
}

void CaptureProtection::unloadScript()
{
    if (!m_pluginName.isEmpty() && !m_kwinOwner.isEmpty()) {
        auto call = scriptCall(m_kwinOwner, QStringLiteral("unloadScript"));
        call << m_pluginName;
        QDBusConnection::sessionBus().call(call, QDBus::Block, 1000);
    }
    m_pluginName.clear();
    m_token.clear();
    m_scriptDirectory.reset();
}

void CaptureProtection::ownerChanged(const QString &, const QString &oldOwner, const QString &newOwner)
{
    if (!m_requested)
        return;
    if (!oldOwner.isEmpty() && oldOwner == m_kwinOwner) {
        fail(tr("KWin перезапустился. Оверлей скрыт до восстановления защиты записи экрана."));
        // The old process no longer exists; don't send its script id/name to
        // the replacement. Stale asynchronous replies are invalidated by token.
        m_kwinOwner.clear();
        m_pluginName.clear();
        m_token.clear();
        m_scriptDirectory.reset();
    }
    if (!newOwner.isEmpty())
        QTimer::singleShot(0, this, &CaptureProtection::start);
}
