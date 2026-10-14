#pragma once

#include <QString>

struct AppSettings {
    QString apiKey;
    QString model = QStringLiteral("google/gemini-2.5-flash-lite");
    QString captureShortcut = QStringLiteral("Ctrl+Alt+Space");
    QString clearShortcut = QStringLiteral("Ctrl+Alt+Backspace");
    int displaySeconds = 15;
    int fontPixels = 32;
    bool currentMonitor = true;
};

QString settingsFilePath();
AppSettings loadSettings(QString *error = nullptr);
bool saveSettings(const AppSettings &settings, QString *error = nullptr);
QString effectiveApiKey(const AppSettings &settings);
