#include "mousebindings.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDebug>

namespace {
QString canonicalKey(int button)
{
    if (button == 2)
        return QStringLiteral("MiddleButton");
    return button == 4 ? QStringLiteral("ExtraButton1") : QStringLiteral("ExtraButton2");
}

QStringList aliases(int button)
{
    if (button == 2)
        return {QStringLiteral("MiddleButton")};
    return button == 4
        ? QStringList{QStringLiteral("ExtraButton1"), QStringLiteral("BackButton"), QStringLiteral("XButton1")}
        : QStringList{QStringLiteral("ExtraButton2"), QStringLiteral("ForwardButton"), QStringLiteral("XButton2")};
}

KConfigGroup mouseGroup(KConfig *config)
{
    return config->group(QStringLiteral("ButtonRebinds")).group(QStringLiteral("Mouse"));
}
}

MouseBindings::MouseBindings() = default;

MouseBindings::~MouseBindings()
{
    if (m_initialized && !restore())
        qWarning("Could not restore mouse bindings; recovery journal retained for next launch.");
}

MouseBindings::Entries MouseBindings::entries(int button) const
{
    const auto group = mouseGroup(m_config.get());
    Entries result;
    for (const auto &key : aliases(button)) {
        if (group.hasKey(key))
            result.insert(key, group.readEntry(key, QStringList{}));
    }
    return result;
}

void MouseBindings::writeEntries(int button, const Entries &values)
{
    auto group = mouseGroup(m_config.get());
    for (const auto &key : aliases(button)) {
        if (values.contains(key))
            group.writeEntry(key, values.value(key), KConfig::Notify);
        else
            group.deleteEntry(key, KConfig::Notify);
    }
}

bool MouseBindings::owns(int button, const Binding &binding) const
{
    const auto current = entries(button);
    if (current.size() != 1 || !current.contains(canonicalKey(button)))
        return false;
    const auto value = current.value(canonicalKey(button));
    return value.size() == 2 && value.first() == QLatin1String("Key")
        && binding.sequences.contains(value.last());
}

MouseBindings::Entries MouseBindings::releasedEntries(int button, const Binding &binding) const
{
    if (owns(button, binding))
        return binding.original;
    auto current = entries(button);
    const auto value = current.value(canonicalKey(button));
    // An external alias may have been added without removing our canonical
    // entry. Keep that new mapping and remove only the entry we installed.
    if (value.size() == 2 && value.first() == QLatin1String("Key")
        && binding.sequences.contains(value.last()))
        current.remove(canonicalKey(button));
    return current;
}

bool MouseBindings::writeJournal(const Bindings &bindings)
{
    for (const int button : {2, 4, 5}) {
        const auto name = canonicalKey(button);
        m_journal->deleteGroup(name);
        const auto it = bindings.constFind(button);
        if (it == bindings.cend())
            continue;
        auto group = m_journal->group(name);
        group.writeEntry("Sequences", it->sequences);
        auto original = group.group(QStringLiteral("Original"));
        for (auto entry = it->original.cbegin(); entry != it->original.cend(); ++entry)
            original.writeEntry(entry.key(), entry.value());
    }
    const bool saved = m_journal->sync();
    if (!saved)
        m_journal->markAsClean();
    return saved;
}

bool MouseBindings::restore()
{
    if (m_bindings.isEmpty())
        return true;
    m_config->reparseConfiguration();
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        // A change made in System Settings while we ran belongs to the user.
        writeEntries(it.key(), releasedEntries(it.key(), it.value()));
    }
    if (!m_config->sync()) {
        m_config->markAsClean();
        return false;
    }
    m_bindings.clear();
    return writeJournal({});
}

bool MouseBindings::recover(QString *error)
{
    if (error)
        error->clear();
    if (m_recovered)
        return true;
    if (!m_initialized) {
        m_config = std::make_unique<KConfig>(QStringLiteral("kcminputrc"), KConfig::NoGlobals);
        m_journal = std::make_unique<KConfig>(QStringLiteral("mini-answer/mouse-bindingsrc"), KConfig::SimpleConfig);
        for (const int button : {2, 4, 5}) {
            const auto group = m_journal->group(canonicalKey(button));
            Binding binding;
            binding.sequences = group.readEntry("Sequences", QStringList{});
            if (binding.sequences.isEmpty())
                continue;
            const auto original = group.group(QStringLiteral("Original"));
            for (const auto &key : aliases(button)) {
                if (original.hasKey(key))
                    binding.original.insert(key, original.readEntry(key, QStringList{}));
            }
            m_bindings.insert(button, binding);
        }
        m_initialized = true;
    }
    if (!restore()) {
        if (error)
            *error = QStringLiteral("Не удалось восстановить прежние назначения мыши после прошлого запуска.");
        return false;
    }
    m_recovered = true;
    return true;
}

bool MouseBindings::configure(const QKeySequence &capture, const QKeySequence &clear,
                              int captureButton, int clearButton, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (error)
        error->clear();
    auto valid = [](int button) { return button == 0 || button == 2 || button == 4 || button == 5; };
    if (!valid(captureButton) || !valid(clearButton)
        || (captureButton != 0 && captureButton == clearButton))
        return fail(QStringLiteral("Выберите разные кнопки мыши для двух действий."));
    if ((captureButton && (capture.isEmpty() || capture.count() != 1))
        || (clearButton && (clear.isEmpty() || clear.count() != 1)))
        return fail(QStringLiteral("Для кнопки мыши нужно одно сочетание клавиш."));
    if (!recover(error))
        return false;

    m_config->reparseConfiguration();
    QMap<int, QString> requested;
    if (captureButton)
        requested.insert(captureButton, capture.toString(QKeySequence::PortableText));
    if (clearButton)
        requested.insert(clearButton, clear.toString(QKeySequence::PortableText));

    Bindings next;
    Bindings pending;
    QMap<int, Entries> before;
    QMap<int, Entries> after;
    const auto group = mouseGroup(m_config.get());
    for (const int button : {2, 4, 5}) {
        if (!requested.contains(button) && !m_bindings.contains(button))
            continue;
        before.insert(button, entries(button));
        const auto original = m_bindings.contains(button)
            ? releasedEntries(button, m_bindings.value(button)) : before.value(button);
        Binding binding{original, m_bindings.value(button).sequences};
        if (requested.contains(button)) {
            const auto sequence = requested.value(button);
            if (!binding.sequences.contains(sequence))
                binding.sequences.append(sequence);
            pending.insert(button, binding);
            next.insert(button, Binding{binding.original, {sequence}});
            after.insert(button, Entries{{canonicalKey(button), {QStringLiteral("Key"), sequence}}});
        } else {
            after.insert(button, original);
            pending.insert(button, binding);
        }
        for (const auto &key : aliases(button)) {
            if (group.isEntryImmutable(key))
                return fail(QStringLiteral("Назначения кнопок мыши заблокированы в настройках KDE."));
        }
    }
    if (after.isEmpty())
        return true;
    // Journal both the old and new values before touching KDE's configuration.
    if (!writeJournal(pending))
        return fail(QStringLiteral("Не удалось сохранить прежние назначения кнопок мыши."));
    for (auto it = after.cbegin(); it != after.cend(); ++it)
        writeEntries(it.key(), it.value());
    if (!m_config->sync()) {
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            writeEntries(it.key(), it.value());
        if (!m_config->sync()) {
            m_config->markAsClean();
            m_bindings = pending;
        } else {
            writeJournal(m_bindings);
        }
        return fail(QStringLiteral("Не удалось сохранить назначения мыши в KDE."));
    }
    m_bindings = next;
    // A failed cleanup leaves a conservative journal that is safe to replay.
    writeJournal(next);
    return true;
}
