#pragma once

#include "core/search/matcher.h"
#include "model/match_highlighter.h"

#include <QObject>
#include <QString>

#include <tl/expected.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QByteArray;
class QSettings;

namespace lv::model
{

// Color as 0xAARRGGBB (lv_model uses QtCore only, so no QColor).
using Argb = std::uint32_t;

enum class HighlightScope : std::uint8_t
{
    kWholeLine,  // colors the whole line; the first matching rule wins
    kMatchOnly,  // colors the matched text; all matching rules are layered in order
};

// One rule (SPEC F-HL-1). An unset color leaves the color of the line (or layer below) as is.
struct HighlightRule
{
    QString pattern;
    bool regex = false;
    bool caseSensitive = false;
    HighlightScope scope = HighlightScope::kWholeLine;
    std::optional<Argb> foreground;
    std::optional<Argb> background;
    bool enabled = true;

    friend bool operator==(const HighlightRule&, const HighlightRule&) = default;
};

// A named, ordered list of rules (F-HL-3).
struct HighlightSet
{
    QString name;
    std::vector<HighlightRule> rules;

    friend bool operator==(const HighlightSet&, const HighlightSet&) = default;
};

// Text range of a match-only rule with its colors.
struct HighlightSpan
{
    HighlightRange range;
    std::optional<Argb> foreground;
    std::optional<Argb> background;

    friend bool operator==(const HighlightSpan&, const HighlightSpan&) = default;
};

// How one line is painted: whole-line colors (from the first matching whole-line rule) and the
// spans of match-only rules in rule order (later spans are painted on top).
struct LineHighlight
{
    std::optional<Argb> foreground;
    std::optional<Argb> background;
    std::vector<HighlightSpan> spans;

    [[nodiscard]] bool empty() const noexcept
    {
        return !foreground && !background && spans.empty();
    }

    friend bool operator==(const LineHighlight&, const LineHighlight&) = default;
};

// The enabled rules of a set, compiled (F-HL-2). Rules that fail to compile (invalid regex) are
// skipped and reported in errors(). Immutable; evaluate() needs a scratch per thread.
class CompiledHighlightSet
{
public:
    struct RuleError
    {
        std::size_t rule = 0;  // index in the set
        QString message;
    };

    explicit CompiledHighlightSet(const HighlightSet& set);
    ~CompiledHighlightSet();
    CompiledHighlightSet(const CompiledHighlightSet&) = delete;
    CompiledHighlightSet& operator=(const CompiledHighlightSet&) = delete;
    CompiledHighlightSet(CompiledHighlightSet&&) = delete;
    CompiledHighlightSet& operator=(CompiledHighlightSet&&) = delete;

    [[nodiscard]] LineHighlight evaluate(const QString& text, core::MatchScratch& scratch) const;

    [[nodiscard]] const std::vector<RuleError>& errors() const noexcept { return m_errors; }

    [[nodiscard]] bool empty() const noexcept { return m_rules.empty(); }

private:
    struct Compiled;
    std::vector<Compiled> m_rules;
    std::vector<RuleError> m_errors;
};

// All highlight sets and the default set (F-HL-3, F-HL-6). There is always at least one set.
// compiled() caches the compiled form of each set until the set changes.
//
// Thread-safety: UI thread only; the compiled sets may be used from any thread.
class HighlightSets : public QObject
{
    Q_OBJECT

public:
    explicit HighlightSets(QObject* parent = nullptr);
    ~HighlightSets() override;
    HighlightSets(const HighlightSets&) = delete;
    HighlightSets& operator=(const HighlightSets&) = delete;
    HighlightSets(HighlightSets&&) = delete;
    HighlightSets& operator=(HighlightSets&&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return m_sets.size(); }

    [[nodiscard]] const HighlightSet& set(std::size_t index) const;

    [[nodiscard]] const std::vector<HighlightSet>& sets() const noexcept { return m_sets; }

    // Index of the set named `name`, if any.
    [[nodiscard]] std::optional<std::size_t> find(const QString& name) const;

    // Adds a set; a name that is taken gets a " (2)", " (3)", ... suffix. Returns its index.
    std::size_t add(HighlightSet set);
    // Replaces set `index` (rules, order, or name; a taken name gets a suffix).
    void replace(std::size_t index, HighlightSet set);
    // Removes set `index`; the last set cannot be removed (returns false).
    bool remove(std::size_t index);

    [[nodiscard]] std::size_t defaultIndex() const noexcept { return m_default; }

    void setDefaultIndex(std::size_t index);

    // Compiled form of set `index`, cached.
    [[nodiscard]] std::shared_ptr<const CompiledHighlightSet> compiled(std::size_t index) const;

    // Persistence in the settings INI (group "Highlight").
    void load(QSettings& settings);
    void save(QSettings& settings) const;

    // JSON import/export of sets (F-HL-6). Import validates everything and returns an error
    // message (nothing is added) on bad input.
    [[nodiscard]] static QByteArray toJson(const std::vector<HighlightSet>& sets);
    [[nodiscard]] static tl::expected<std::vector<HighlightSet>, QString>
    fromJson(const QByteArray& json);

signals:
    // A set changed (index), or sets were added/removed (all).
    void setChanged(std::size_t index);
    void setsChanged();
    void defaultChanged();

private:
    [[nodiscard]] QString uniqueName(const QString& name, std::optional<std::size_t> except) const;

    std::vector<HighlightSet> m_sets;
    std::size_t m_default = 0;
    mutable std::vector<std::shared_ptr<const CompiledHighlightSet>> m_compiled;
};

}  // namespace lv::model
