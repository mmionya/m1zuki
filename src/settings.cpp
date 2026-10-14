#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace {
constexpr qint64 maximumSettingsBytes = 64 * 1024;
constexpr auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto directoryPermissions = filePermissions | QFileDevice::ExeOwner;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool validShortcut(const QString &text)
{
    if (text.isEmpty() || text.size() > 128)
        return false;
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence.isEmpty())
        return false;
    const auto key = sequence[0].key();
    return key != Qt::Key_unknown && key != 0 && key != Qt::Key_Control
        && key != Qt::Key_Shift && key != Qt::Key_Alt && key != Qt::Key_Meta
        && key != Qt::Key_AltGr;
}

bool validate(const AppSettings &settings, QString *error)
{
    static const QRegularExpression modelPattern(
        QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._:@-]*\\z"));
    static const QRegularExpression invalidKeyCharacters(QStringLiteral("[\\s\\x{0000}-\\x{001f}\\x{007f}]"));

    if (settings.apiKey.size() > 4096 || invalidKeyCharacters.match(settings.apiKey).hasMatch()) {
        setError(error, QStringLiteral("API-ключ не должен содержать пробелы или управляющие символы."));
        return false;
    }
    if (settings.model.size() > 256 || !modelPattern.match(settings.model).hasMatch()) {
        setError(error, QStringLiteral("Укажите идентификатор модели в формате провайдер/модель."));
        return false;
    }
    if (!validShortcut(settings.captureShortcut) || !validShortcut(settings.clearShortcut)) {
        setError(error, QStringLiteral("Горячая клавиша должна быть одним допустимым сочетанием, например Ctrl+Alt+Space."));
        return false;
    }
    if (QKeySequence::fromString(settings.captureShortcut, QKeySequence::PortableText)
        == QKeySequence::fromString(settings.clearShortcut, QKeySequence::PortableText)) {
        setError(error, QStringLiteral("Для распознавания и скрытия ответа нужны разные горячие клавиши."));
        return false;
    }
    if (settings.displaySeconds < 1 || settings.displaySeconds > 300) {
        setError(error, QStringLiteral("Время показа ответа должно быть от 1 до 300 секунд."));
        return false;
    }
    if (settings.fontPixels < 12 || settings.fontPixels > 96) {
        setError(error, QStringLiteral("Размер шрифта должен быть от 12 до 96 пикселей."));
        return false;
    }
    return true;
}

bool readString(const QJsonObject &object, const QString &name, QString &target, QString *error)
{
    const auto value = object.value(name);
    if (value.isUndefined())
        return true;
    if (!value.isString()) {
        setError(error, QStringLiteral("Поле настроек «%1» должно быть строкой.").arg(name));
        return false;
    }
    target = value.toString();
    return true;
}

bool readInteger(const QJsonObject &object, const QString &name, int minimum, int maximum,
                 int &target, QString *error)
{
    const auto value = object.value(name);
    if (value.isUndefined())
        return true;
    const auto number = value.toDouble();
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum) {
        setError(error, QStringLiteral("Поле настроек «%1» должно быть целым числом от %2 до %3.")
                            .arg(name).arg(minimum).arg(maximum));
        return false;
    }
    target = static_cast<int>(number);
    return true;
}
} // namespace

QString settingsFilePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("mini-answer/settings.json"));
}

AppSettings loadSettings(QString *error)
{
    setError(error, {});
    const auto path = settingsFilePath();
    const QFileInfo info(path);
    if (info.isSymLink()) {
        setError(error, QStringLiteral("Файл настроек не должен быть символической ссылкой."));
        return {};
    }
    if (!info.exists())
        return {};

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Не удалось прочитать файл настроек: %1").arg(file.errorString()));
        return {};
    }
    const auto contents = file.read(maximumSettingsBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        setError(error, QStringLiteral("Ошибка чтения файла настроек: %1").arg(file.errorString()));
        return {};
    }
    if (contents.size() > maximumSettingsBytes) {
        setError(error, QStringLiteral("Файл настроек слишком большой (максимум 64 КиБ)."));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // Do not include parser context: it could contain the saved API key.
        setError(error, QStringLiteral("Файл настроек содержит некорректный JSON; нужен объект настроек."));
        return {};
    }

    const auto object = document.object();
    AppSettings settings;
    if (!readString(object, QStringLiteral("apiKey"), settings.apiKey, error)
        || !readString(object, QStringLiteral("model"), settings.model, error)
        || !readString(object, QStringLiteral("captureShortcut"), settings.captureShortcut, error)
        || !readString(object, QStringLiteral("clearShortcut"), settings.clearShortcut, error)
        || !readInteger(object, QStringLiteral("displaySeconds"), 1, 300, settings.displaySeconds, error)
        || !readInteger(object, QStringLiteral("fontPixels"), 12, 96, settings.fontPixels, error))
        return {};

    const auto monitor = object.value(QStringLiteral("currentMonitor"));
    if (!monitor.isUndefined()) {
        if (!monitor.isBool()) {
            setError(error, QStringLiteral("Поле настроек «currentMonitor» должно быть логическим значением."));
            return {};
        }
        settings.currentMonitor = monitor.toBool();
    }
    if (!validate(settings, error))
        return {};
    return settings;
}

bool saveSettings(const AppSettings &settings, QString *error)
{
    setError(error, {});
    if (!validate(settings, error))
        return false;

    const auto path = settingsFilePath();
    const auto directory = QFileInfo(path).absolutePath();
    if (QFileInfo(directory).isSymLink() || QFileInfo(path).isSymLink()) {
        setError(error, QStringLiteral("Каталог и файл настроек не должны быть символическими ссылками."));
        return false;
    }
    if (!QDir().mkpath(directory) || !QFile::setPermissions(directory, directoryPermissions)) {
        setError(error, QStringLiteral("Не удалось создать личный каталог настроек с правами 0700."));
        return false;
    }

    const QJsonObject object{
        {QStringLiteral("apiKey"), settings.apiKey},
        {QStringLiteral("model"), settings.model},
        {QStringLiteral("captureShortcut"), settings.captureShortcut},
        {QStringLiteral("clearShortcut"), settings.clearShortcut},
        {QStringLiteral("displaySeconds"), settings.displaySeconds},
        {QStringLiteral("fontPixels"), settings.fontPixels},
        {QStringLiteral("currentMonitor"), settings.currentMonitor},
    };
    const auto contents = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Не удалось открыть файл настроек для записи: %1").arg(file.errorString()));
        return false;
    }
    if (!file.setPermissions(filePermissions)) {
        file.cancelWriting();
        setError(error, QStringLiteral("Не удалось установить права 0600 для файла настроек."));
        return false;
    }
    if (file.write(contents) != contents.size()) {
        file.cancelWriting();
        setError(error, QStringLiteral("Не удалось записать настройки: %1").arg(file.errorString()));
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("Не удалось сохранить настройки: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

QString effectiveApiKey(const AppSettings &settings)
{
    if (!settings.apiKey.isEmpty())
        return settings.apiKey;
    return qEnvironmentVariable("OPENROUTER_API_KEY").trimmed();
}
