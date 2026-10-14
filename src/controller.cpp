#include "controller.h"
#include "desktop.h"
#include "openrouter.h"
#include "overlay.h"
#include "captureprotection.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

static QIcon appIcon() {
    return QIcon(":/mini-answer-character.png");
}

Controller::Controller(bool demoMode) : demoMode_(demoMode) {
    QString loadError;
    settings_ = demoMode_ ? AppSettings{} : loadSettings(&loadError);
    desktop_ = new DesktopIntegration(this);
    client_ = new OpenRouterClient(this);
    overlay_ = new Overlay;
    protection_ = new CaptureProtection(this);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        clear();
        delete overlay_;
        overlay_ = nullptr;
    });
    setWindowIcon(appIcon());
    qApp->setWindowIcon(windowIcon());
    buildUi();
    connect(protection_, &CaptureProtection::statusChanged, this, [this](bool ready, const QString &message) {
        const bool showingProtectionError = !statusLabel_->isHidden() && statusLabel_->text() == protectionStatus_;
        protectionStatus_ = message;
        if (!ready) {
            const QString previousError = statusLabel_->isHidden() ? QString{} : statusLabel_->text();
            clear();
            if (protection_->hasFailed()) status(message, true);
            else if (!previousError.isEmpty() && !showingProtectionError) status(previousError, true);
            else if (tray_) tray_->setToolTip("m1zuki — " + message);
            return;
        }
        if (showingProtectionError) status(message);
        if (tray_) tray_->setToolTip("m1zuki — " + message);
        if (previewPending_) {
            previewPending_ = false;
            preview();
        }
    });
    if (!demoMode_) {
        setupTray();
        applyShortcuts();
        QTimer::singleShot(0, protection_, &CaptureProtection::start);
    }
    if (!loadError.isEmpty()) status(loadError, true);
    connect(desktop_, &DesktopIntegration::captureRequested, this, &Controller::capture);
    connect(desktop_, &DesktopIntegration::clearRequested, this, &Controller::clear);
    connect(desktop_, &DesktopIntegration::failed, this, [this](const QString &message) {
        if (!busy_) return;
        busy_ = false;
        status(message, true);
    });
    connect(desktop_, &DesktopIntegration::captured, this, [this](QImage image) {
        if (!busy_ || demoMode_) return;
        // Preserve readable text while bounding upload size on high-resolution desktops.
        if (image.width() > 2560 || image.height() > 2560)
            image = image.scaled(2560, 2560, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            busy_ = false; status("Не удалось подготовить снимок экрана.", true); return;
        }
        status("Ожидаю ответ OpenRouter…");
        client_->solve(png, effectiveApiKey(settings_), settings_.model);
    });
    connect(client_, &OpenRouterClient::answered, this, [this](const QList<int> &numbers) {
        if (!busy_) return;
        busy_ = false;
        if (!protection_->isReady()) {
            status("Защита записи экрана недоступна. Ответ не показан.", true);
            return;
        }
        overlay_->showNumbers(numbers, settings_.displaySeconds, settings_.fontPixels);
        status("Ответ показан. Готов к следующему вопросу.");
    });
    connect(client_, &OpenRouterClient::failed, this, [this](const QString &message) {
        if (!busy_) return;
        busy_ = false;
        status(message, true);
    });
}

void Controller::buildUi() {
    setWindowTitle("m1zuki — настройки");
    setObjectName("settingsWindow");
    setMinimumWidth(560);
    setStyleSheet(R"(
      QWidget { color: #f4eff6; font-family: "Adwaita Sans", "Noto Sans", sans-serif; font-size: 14px; }
      QWidget#settingsWindow { background: #15131c; }
      QFrame#card { background: #1d1b26; border: 1px solid #302c3c; border-radius: 18px; }
      QLabel { background: transparent; color: #c6bdcf; font-size: 12px; font-weight: 500; }
      QFrame#divider { background: #353040; border: none; }
      QLineEdit, QKeySequenceEdit, QSpinBox {
        background: #25222f; border: 1px solid #3a3447; border-radius: 9px;
        padding: 10px 12px; selection-background-color: #795568; selection-color: #ffffff;
      }
      QLineEdit:hover, QKeySequenceEdit:hover, QSpinBox:hover { border-color: #65556e; }
      QLineEdit:focus, QKeySequenceEdit:focus, QSpinBox:focus { border-color: #e4aec6; background: #2b2534; }
      QKeySequenceEdit { background: transparent; border: none; padding: 0; }
      QKeySequenceEdit QLineEdit { background: #25222f; border: 1px solid #3a3447; border-radius: 9px; padding: 10px 12px; }
      QKeySequenceEdit QLineEdit:hover { border-color: #65556e; }
      QKeySequenceEdit QLineEdit:focus { border-color: #e4aec6; background: #2b2534; }
      QSpinBox { padding-right: 30px; }
      QSpinBox QLineEdit { background: transparent; border: none; padding: 0 0 0 10px; }
      QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 28px; border: none; border-left: 1px solid #3a3447; border-top-right-radius: 9px; }
      QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 28px; border: none; border-left: 1px solid #3a3447; border-bottom-right-radius: 9px; }
      QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #403348; }
      QSpinBox::up-arrow { image: url(:/chevron-up.svg); width: 12px; height: 8px; }
      QSpinBox::down-arrow { image: url(:/chevron-down.svg); width: 12px; height: 8px; }
      QPushButton {
        background: #272230; border: 1px solid #403649; border-radius: 10px;
        padding: 11px 16px; font-size: 13px; font-weight: 600;
      }
      QPushButton:hover { background: #352b40; border-color: #73617f; }
      QPushButton:pressed { background: #41334d; }
      QPushButton:focus { border-color: #e4aec6; }
      QPushButton#primary { background: #e4aec6; border: 1px solid #e4aec6; color: #30202e; }
      QPushButton#primary:hover { background: #f1c3d7; border-color: #f1c3d7; }
      QPushButton#primary:pressed { background: #d39ab5; }
      QPushButton#primary:focus { border-color: #fff0f7; }
      QPushButton#quiet { background: transparent; border-color: transparent; color: #a99db6; }
      QPushButton#quiet:hover { background: #272230; color: #f4eff6; }
      QPushButton#quiet:focus { border-color: #e4aec6; }
      QCheckBox { background: transparent; spacing: 10px; font-size: 13px; color: #c6bdcf; }
      QCheckBox::indicator { width: 18px; height: 18px; border-radius: 5px; border: 1px solid #62536e; background: #25222f; }
      QCheckBox::indicator:checked { background: #e4aec6; border-color: #e4aec6; image: url(:/check.svg); }
      QCheckBox::indicator:hover, QCheckBox::indicator:focus { border-color: #e4aec6; }
      QLabel#error { color: #f4b6c3; background: #35232e; border: 1px solid #674050; border-radius: 10px; padding: 12px; font-weight: 400; }
      QMenu { background: #1d1b26; border: 1px solid #403649; padding: 6px; }
      QMenu::item { padding: 8px 18px; border-radius: 5px; }
      QMenu::item:selected { background: #403348; }
      QMenu::separator { height: 1px; background: #403649; margin: 5px 10px; }
      QToolTip { color: #f4eff6; background: #302838; border: 1px solid #62536e; padding: 5px; }
    )");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 22, 22, 18);
    layout->setSpacing(18);

    auto *card = new QFrame(this); card->setObjectName("card");
    auto *form = new QGridLayout(card);
    form->setContentsMargins(22, 22, 22, 22);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(8);
    form->setColumnStretch(0, 1);
    form->setColumnStretch(1, 1);
    auto addField = [form, card](const QString &text, QWidget *field, int row, int column, int span = 1) {
        auto *label = new QLabel(text, card);
        label->setBuddy(field);
        field->setAccessibleName(text);
        form->addWidget(label, row, column, 1, span);
        form->addWidget(field, row + 1, column, 1, span);
    };
    keyEdit_ = new QLineEdit(settings_.apiKey, card);
    keyEdit_->setEchoMode(QLineEdit::Password);
    keyEdit_->setPlaceholderText(qEnvironmentVariableIsSet("OPENROUTER_API_KEY") ? "Используется OPENROUTER_API_KEY" : "sk-or-v1-…");
    keyEdit_->setToolTip("Ключ сохраняется локально в файле с доступом только для вашего пользователя.");
    modelEdit_ = new QLineEdit(settings_.model, card);
    modelEdit_->setPlaceholderText("provider/model");
    modelEdit_->setToolTip("ID модели OpenRouter с поддержкой изображений. Можно вставить свой.");
    captureEdit_ = new QKeySequenceEdit(QKeySequence(settings_.captureShortcut), card);
    captureEdit_->setMaximumSequenceLength(1);
    clearEdit_ = new QKeySequenceEdit(QKeySequence(settings_.clearShortcut), card);
    clearEdit_->setMaximumSequenceLength(1);
    secondsEdit_ = new QSpinBox(card); secondsEdit_->setRange(1, 300); secondsEdit_->setSuffix(" сек"); secondsEdit_->setValue(settings_.displaySeconds);
    fontEdit_ = new QSpinBox(card); fontEdit_->setRange(12, 96); fontEdit_->setSuffix(" px"); fontEdit_->setValue(settings_.fontPixels);
    secondsEdit_->setAlignment(Qt::AlignCenter);
    fontEdit_->setAlignment(Qt::AlignCenter);
    monitorEdit_ = new QCheckBox("Только монитор под курсором", card);
    monitorEdit_->setChecked(settings_.currentMonitor);
    monitorEdit_->setToolTip("Если выключено, снимаются все мониторы.");
    addField("API-ключ", keyEdit_, 0, 0, 2);
    form->setRowMinimumHeight(2, 8);
    addField("Модель", modelEdit_, 3, 0, 2);
    auto *divider = new QFrame(card);
    divider->setObjectName("divider");
    divider->setFixedHeight(1);
    form->addWidget(divider, 5, 0, 1, 2, Qt::AlignVCenter);
    form->setRowMinimumHeight(5, 25);
    addField("Получить ответ", captureEdit_, 6, 0);
    addField("Скрыть / отменить", clearEdit_, 6, 1);
    form->setRowMinimumHeight(8, 8);
    addField("Время показа", secondsEdit_, 9, 0);
    addField("Размер шрифта", fontEdit_, 9, 1);
    form->setRowMinimumHeight(11, 8);
    form->addWidget(monitorEdit_, 12, 0, 1, 2);
    layout->addWidget(card);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("error");
    statusLabel_->setWordWrap(true);
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto *test = new QPushButton("Проверить", this);
    test->setToolTip("Показать пример ответа без снимка экрана и запроса к OpenRouter.");
    auto *captureButton = new QPushButton("Снять экран", this);
    auto *saveButton = new QPushButton("Сохранить", this);
    saveButton->setObjectName("primary");
    saveButton->setToolTip("Сохранить настройки и свернуть в трей.");
    buttons->addWidget(test);
    buttons->addWidget(captureButton);
    buttons->addStretch();
    buttons->addWidget(saveButton);
    layout->addLayout(buttons);
    auto *footer = new QHBoxLayout;
    auto *quit = new QPushButton("Выйти", this);
    quit->setObjectName("quiet");
    footer->addStretch(); footer->addWidget(quit);
    layout->addLayout(footer);
    connect(test, &QPushButton::clicked, this, &Controller::preview);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (save() && tray_ && tray_->isVisible()) hide();
    });
    connect(captureButton, &QPushButton::clicked, this, [this] { if (save()) capture(); });
    connect(quit, &QPushButton::clicked, qApp, &QApplication::quit);
    adjustSize();
}

void Controller::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto *menu = new QMenu(this);
    menu->addAction("Получить ответ", this, &Controller::capture);
    menu->addAction("Скрыть ответ / отменить", this, &Controller::clear);
    menu->addSeparator();
    menu->addAction("Настройки", this, &Controller::showSettings);
    menu->addAction("Проверить оверлей", this, &Controller::preview);
    menu->addSeparator();
    menu->addAction("Выйти", qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showSettings();
    });
    tray_->setToolTip("m1zuki");
    tray_->show();
}

bool Controller::save() {
    if (demoMode_) return false;
    AppSettings next;
    next.apiKey = keyEdit_->text().trimmed();
    next.model = modelEdit_->text().trimmed();
    next.captureShortcut = captureEdit_->keySequence().toString(QKeySequence::PortableText);
    next.clearShortcut = clearEdit_->keySequence().toString(QKeySequence::PortableText);
    next.displaySeconds = secondsEdit_->value();
    next.fontPixels = fontEdit_->value();
    next.currentMonitor = monitorEdit_->isChecked();
    QString error;
    if (!desktop_->registerShortcuts(QKeySequence(next.captureShortcut), QKeySequence(next.clearShortcut), &error)) {
        status(error, true); return false;
    }
    if (!saveSettings(next, &error)) {
        desktop_->registerShortcuts(QKeySequence(settings_.captureShortcut), QKeySequence(settings_.clearShortcut));
        status(error, true); return false;
    }
    clear();
    settings_ = next;
    status(configured() ? "Готово. Нажмите " + settings_.captureShortcut + "." : "Настройки сохранены. Для распознавания нужен API-ключ.");
    return true;
}

void Controller::applyShortcuts() {
    QString error;
    if (!desktop_->registerShortcuts(QKeySequence(settings_.captureShortcut), QKeySequence(settings_.clearShortcut), &error)) status(error, true);
    else if (configured()) status("Готово. Нажмите " + settings_.captureShortcut + ".");
}

bool Controller::configured() const { return !effectiveApiKey(settings_).isEmpty(); }

void Controller::capture() {
    if (demoMode_ || busy_) return;
    if (!protection_->isReady()) {
        showSettings();
        if (protectionStatus_.isEmpty()) protectionStatus_ = "Защита записи экрана ещё не готова. Дождитесь подтверждения KDE.";
        status(protectionStatus_, true);
        return;
    }
    if (!configured()) { showSettings(); status("Введите API-ключ OpenRouter и сохраните настройки.", true); return; }
    clear();
    busy_ = true;
    // Let the compositor remove our windows before Spectacle captures the screen.
    hide();
    const auto request = generation_;
    status("Снимаю экран…");
    QTimer::singleShot(250, this, [this, request] {
        if (busy_ && request == generation_) desktop_->captureScreen(settings_.currentMonitor);
    });
}

void Controller::clear() {
    ++generation_;
    previewPending_ = false;
    busy_ = false;
    client_->cancel();
    desktop_->cancelCapture();
    if (overlay_) overlay_->clear();
    status("Готов к следующему вопросу.");
}

void Controller::preview() {
    clear();
    if (!protection_->isReady()) {
        if (protection_->hasFailed()) {
            status(protectionStatus_, true);
            return;
        }
        previewPending_ = true;
        status("Оверлей появится после подтверждения защиты записи экрана.");
        return;
    }
    overlay_->showNumbers({1, 3, 4}, secondsEdit_->value(), fontEdit_->value());
    status("Проверка: 1, 3, 4 в углу. Снимок и API-запрос не выполнялись.");
}

void Controller::status(const QString &message, bool error) {
    statusLabel_->setText(message);
    statusLabel_->setVisible(error);
    if (tray_) {
        tray_->setToolTip("m1zuki — " + message);
        if (error && !isVisible()) tray_->showMessage("m1zuki", message, QSystemTrayIcon::Warning, 5000);
    }
}

void Controller::showSettings() { show(); raise(); activateWindow(); }

void Controller::handleCommand(const QString &command) {
    if (command == "capture") capture();
    else if (command == "clear") clear();
    else if (command == "demo") preview();
    else if (command == "quit") qApp->quit();
    else showSettings();
}

void Controller::closeEvent(QCloseEvent *event) {
    if (tray_ && tray_->isVisible()) { hide(); event->ignore(); }
    else { event->accept(); qApp->quit(); }
}

bool Controller::renderPreview(const QString &path) {
    show();
    qApp->processEvents();
    return grab().save(path);
}
