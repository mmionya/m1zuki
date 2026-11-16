#pragma once

#include <QString>

struct AppSettings {
    QString apiKey;
    QString model = QStringLiteral("google/gemini-2.5-flash-lite");
#ifdef Q_OS_MACOS
    QString captureShortcut = QStringLiteral("Meta+Alt+Space");
    QString clearShortcut = QStringLiteral("Meta+Alt+Backspace");
#else
    QString captureShortcut = QStringLiteral("Ctrl+Alt+Space");
    QString clearShortcut = QStringLiteral("Ctrl+Alt+Backspace");
#endif
    int captureMouseButton = 0;
    int clearMouseButton = 0;
    int displaySeconds = 15;
    int fontPixels = 32;
    bool currentMonitor = true;
    bool allowUnprotectedOverlay = false;
};

QString settingsFilePath();
AppSettings loadSettings(QString *error = nullptr);
bool validateSettings(const AppSettings &settings, QString *error = nullptr);
bool saveSettings(const AppSettings &settings, QString *error = nullptr);
QString effectiveApiKey(const AppSettings &settings);
