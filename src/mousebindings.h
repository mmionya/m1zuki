#pragma once

#include <QKeySequence>
#include <QMap>
#include <QStringList>
#include <memory>

class KConfig;

// KWin's native button rebinding feeds the existing global keyboard shortcuts.
// Keep a journal so a later launch can also undo bindings left by a crash.
class MouseBindings
{
public:
    MouseBindings();
    ~MouseBindings();
    bool recover(QString *error = nullptr);
    bool configure(const QKeySequence &capture, const QKeySequence &clear,
                   int captureButton, int clearButton, QString *error = nullptr);

private:
    using Entries = QMap<QString, QStringList>;
    struct Binding {
        Entries original;
        QStringList sequences;
    };
    using Bindings = QMap<int, Binding>;

    Entries entries(int button) const;
    void writeEntries(int button, const Entries &entries);
    bool owns(int button, const Binding &binding) const;
    Entries releasedEntries(int button, const Binding &binding) const;
    bool writeJournal(const Bindings &bindings);
    bool restore();

    std::unique_ptr<KConfig> m_config;
    std::unique_ptr<KConfig> m_journal;
    Bindings m_bindings;
    bool m_initialized = false;
    bool m_recovered = false;
};
