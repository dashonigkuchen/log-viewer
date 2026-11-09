#pragma once

#include "core/util/strong_types.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace lv::model
{

class Document;

// The bookmarks of one document (SPEC F-BOOK-1 … F-BOOK-4): named lines, by original line
// number. Like marks, they are cleared when the document starts a new generation, and cleared()
// reports how many were lost.
//
// Thread-safety: UI thread only.
class Bookmarks : public QObject
{
    Q_OBJECT

public:
    // Default names are the start of the line (F-BOOK-1).
    static constexpr qsizetype kDefaultNameLength = 60;

    struct Entry
    {
        core::LineNumber line;
        QString name;
    };

    explicit Bookmarks(const Document& document, QObject* parent = nullptr);

    [[nodiscard]] bool contains(core::LineNumber line) const;

    [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

    [[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

    // Bookmarks in line order.
    [[nodiscard]] std::vector<Entry> entries() const;
    [[nodiscard]] std::optional<QString> name(core::LineNumber line) const;

    // Adds a bookmark; an existing one on the same line is renamed.
    void add(core::LineNumber line, const QString& name);
    // Adds a bookmark named after the line text, or removes the existing one (Ctrl+B).
    // Returns true if a bookmark was added.
    bool toggle(core::LineNumber line, const QString& lineText);
    void rename(core::LineNumber line, const QString& name);
    void remove(core::LineNumber line);
    void clear();

    [[nodiscard]] std::optional<core::LineNumber> next(core::LineNumber line) const;
    [[nodiscard]] std::optional<core::LineNumber> previous(core::LineNumber line) const;

    // The default name for a line: its text, trimmed, cut to kDefaultNameLength characters.
    [[nodiscard]] static QString defaultName(const QString& lineText);

signals:
    void changed();
    void cleared(quint64 count);

private:
    std::map<std::uint64_t, QString> m_entries;
};

}  // namespace lv::model
