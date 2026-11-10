#include "model/highlight_sets.h"

#include "core/search/query.h"
#include "core/util/assert.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSettings>

#include <utility>

namespace lv::model
{
namespace
{

constexpr auto kFormat = "logviewer-highlight-sets";
constexpr int kVersion = 1;

QString colorToString(std::optional<Argb> color)
{
    if (!color) {
        return {};
    }
    return QStringLiteral("#%1").arg(*color, 8, 16, QChar(u'0')).toUpper();
}

// "#RRGGBB" (opaque) or "#AARRGGBB"; empty or null = unset.
std::expected<std::optional<Argb>, QString> colorFromJson(const QJsonValue& value)
{
    if (value.isNull() || value.isUndefined()) {
        return std::optional<Argb>{};
    }
    if (!value.isString()) {
        return std::unexpected(QStringLiteral("a color must be a string like \"#RRGGBB\""));
    }
    const QString text = value.toString();
    if (text.isEmpty()) {
        return std::optional<Argb>{};
    }
    bool ok = false;
    const uint parsed = text.mid(1).toUInt(&ok, 16);
    if (!text.startsWith(u'#') || !ok || (text.size() != 7 && text.size() != 9)) {
        return std::unexpected(QStringLiteral("invalid color \"%1\"").arg(text));
    }
    return std::optional<Argb>(text.size() == 7 ? (0xFF000000U | parsed) : parsed);
}

QJsonObject ruleToJson(const HighlightRule& rule)
{
    QJsonObject object{
        {QStringLiteral("pattern"), rule.pattern},
        {QStringLiteral("regex"), rule.regex},
        {QStringLiteral("caseSensitive"), rule.caseSensitive},
        {QStringLiteral("scope"), rule.scope == HighlightScope::kWholeLine
                                      ? QStringLiteral("line")
                                      : QStringLiteral("match")},
        {QStringLiteral("enabled"), rule.enabled},
    };
    object.insert(QStringLiteral("foreground"), rule.foreground
                                                    ? QJsonValue(colorToString(rule.foreground))
                                                    : QJsonValue(QJsonValue::Null));
    object.insert(QStringLiteral("background"), rule.background
                                                    ? QJsonValue(colorToString(rule.background))
                                                    : QJsonValue(QJsonValue::Null));
    return object;
}

std::expected<HighlightRule, QString> ruleFromJson(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::unexpected(QStringLiteral("a rule must be an object"));
    }
    const QJsonObject object = value.toObject();
    HighlightRule rule;
    const QJsonValue pattern = object.value(QStringLiteral("pattern"));
    if (!pattern.isString() || pattern.toString().isEmpty()) {
        return std::unexpected(QStringLiteral("a rule needs a non-empty \"pattern\""));
    }
    rule.pattern = pattern.toString();
    rule.regex = object.value(QStringLiteral("regex")).toBool(false);
    rule.caseSensitive = object.value(QStringLiteral("caseSensitive")).toBool(false);
    rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    const QString scope = object.value(QStringLiteral("scope")).toString(QStringLiteral("line"));
    if (scope == QStringLiteral("line")) {
        rule.scope = HighlightScope::kWholeLine;
    } else if (scope == QStringLiteral("match")) {
        rule.scope = HighlightScope::kMatchOnly;
    } else {
        return std::unexpected(
            QStringLiteral("unknown scope \"%1\" (use \"line\" or \"match\")").arg(scope));
    }
    auto foreground = colorFromJson(object.value(QStringLiteral("foreground")));
    if (!foreground) {
        return std::unexpected(foreground.error());
    }
    auto background = colorFromJson(object.value(QStringLiteral("background")));
    if (!background) {
        return std::unexpected(background.error());
    }
    rule.foreground = *foreground;
    rule.background = *background;
    return rule;
}

HighlightSet defaultSet()
{
    // A useful start: errors red, warnings amber (whole line).
    return HighlightSet{.name = QStringLiteral("Default"),
                        .rules = {
                            HighlightRule{.pattern = QStringLiteral("\\b(ERROR|FATAL|CRITICAL)\\b"),
                                          .regex = true,
                                          .caseSensitive = false,
                                          .scope = HighlightScope::kWholeLine,
                                          .foreground = std::nullopt,
                                          .background = 0xFFFFCDD2U,
                                          .enabled = true},
                            HighlightRule{.pattern = QStringLiteral("\\bWARN(ING)?\\b"),
                                          .regex = true,
                                          .caseSensitive = false,
                                          .scope = HighlightScope::kWholeLine,
                                          .foreground = std::nullopt,
                                          .background = 0xFFFFF3C4U,
                                          .enabled = true},
                        }};
}

}  // namespace

// ------------------------------------------------------------------------------------------
// CompiledHighlightSet

struct CompiledHighlightSet::Compiled
{
    std::shared_ptr<const core::CompiledQuery> query;
    HighlightScope scope = HighlightScope::kWholeLine;
    std::optional<Argb> foreground;
    std::optional<Argb> background;
};

CompiledHighlightSet::CompiledHighlightSet(const HighlightSet& set)
{
    for (std::size_t i = 0; i < set.rules.size(); ++i) {
        const HighlightRule& rule = set.rules[i];
        if (!rule.enabled || rule.pattern.isEmpty()) {
            continue;
        }
        const core::Query query{.text = rule.pattern.toStdString(),
                                .mode = rule.regex ? core::QueryMode::kRegex
                                                   : core::QueryMode::kLiteral,
                                .caseSensitive = rule.caseSensitive,
                                .wholeWord = false,
                                .invert = false};
        auto compiled = core::CompiledQuery::compile(query);
        if (!compiled) {
            m_errors.push_back(
                {.rule = i, .message = QString::fromStdString(compiled.error().message)});
            continue;
        }
        m_rules.push_back({.query = std::move(*compiled),
                           .scope = rule.scope,
                           .foreground = rule.foreground,
                           .background = rule.background});
    }
}

CompiledHighlightSet::~CompiledHighlightSet() = default;

LineHighlight CompiledHighlightSet::evaluate(const QString& text, core::MatchScratch& scratch) const
{
    LineHighlight out;
    bool lineDone = false;
    for (const Compiled& rule : m_rules) {
        if (rule.scope == HighlightScope::kWholeLine) {
            if (lineDone) {
                continue;
            }
            // Whole-line rules only need a yes/no; ranges tell us that as well.
            if (!highlightRanges(*rule.query, text, scratch).empty()) {
                out.foreground = rule.foreground;
                out.background = rule.background;
                lineDone = true;
            }
            continue;
        }
        for (const HighlightRange& range : highlightRanges(*rule.query, text, scratch)) {
            out.spans.push_back(
                {.range = range, .foreground = rule.foreground, .background = rule.background});
        }
    }
    return out;
}

// ------------------------------------------------------------------------------------------
// HighlightSets

HighlightSets::HighlightSets(QObject* parent)
    : QObject(parent)
    , m_sets{defaultSet()}
    , m_compiled(1)
{}

HighlightSets::~HighlightSets() = default;

const HighlightSet& HighlightSets::set(std::size_t index) const
{
    LV_ASSERT(index < m_sets.size());
    return m_sets[index];
}

std::optional<std::size_t> HighlightSets::find(const QString& name) const
{
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        if (m_sets[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

QString HighlightSets::uniqueName(const QString& name, std::optional<std::size_t> except) const
{
    const QString base =
        name.trimmed().isEmpty() ? QStringLiteral("Highlight Set") : name.trimmed();
    QString candidate = base;
    for (int n = 2;; ++n) {
        const auto found = find(candidate);
        if (!found || found == except) {
            return candidate;
        }
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
    }
}

std::size_t HighlightSets::add(HighlightSet set)
{
    set.name = uniqueName(set.name, std::nullopt);
    m_sets.push_back(std::move(set));
    m_compiled.emplace_back();
    emit setsChanged();
    return m_sets.size() - 1;
}

void HighlightSets::replace(std::size_t index, HighlightSet set)
{
    LV_ASSERT(index < m_sets.size());
    set.name = uniqueName(set.name, index);
    if (set == m_sets[index]) {
        return;
    }
    m_sets[index] = std::move(set);
    m_compiled[index].reset();
    emit setChanged(index);
}

bool HighlightSets::remove(std::size_t index)
{
    if (m_sets.size() <= 1 || index >= m_sets.size()) {
        return false;
    }
    m_sets.erase(m_sets.begin() + static_cast<std::ptrdiff_t>(index));
    m_compiled.erase(m_compiled.begin() + static_cast<std::ptrdiff_t>(index));
    // Removing the default makes the set before it (or the new first set) the default.
    const bool defaultRemoved = index == m_default;
    if (index < m_default || (defaultRemoved && m_default > 0)) {
        --m_default;
    }
    emit setsChanged();
    if (defaultRemoved) {
        emit defaultChanged();
    }
    return true;
}

void HighlightSets::assign(std::vector<HighlightSet> sets, std::size_t defaultIndex)
{
    if (sets.empty()) {
        sets.push_back(defaultSet());
    }
    m_sets.clear();
    for (auto& set : sets) {
        set.name = uniqueName(set.name, std::nullopt);
        m_sets.push_back(std::move(set));
    }
    m_compiled.assign(m_sets.size(), nullptr);
    m_default = defaultIndex < m_sets.size() ? defaultIndex : 0;
    emit setsChanged();
    emit defaultChanged();
}

void HighlightSets::setDefaultIndex(std::size_t index)
{
    LV_ASSERT(index < m_sets.size());
    if (index != m_default) {
        m_default = index;
        emit defaultChanged();
    }
}

std::shared_ptr<const CompiledHighlightSet> HighlightSets::compiled(std::size_t index) const
{
    LV_ASSERT(index < m_sets.size());
    auto& cached = m_compiled[index];
    if (!cached) {
        cached = std::make_shared<const CompiledHighlightSet>(m_sets[index]);
    }
    return cached;
}

void HighlightSets::load(QSettings& settings)
{
    const QByteArray json = settings.value(QStringLiteral("Highlight/sets")).toByteArray();
    if (json.isEmpty()) {
        return;  // keep the built-in default set
    }
    auto sets = fromJson(json);
    if (!sets || sets->empty()) {
        return;  // corrupt entry: keep the defaults (settings recovery is F-SET-3)
    }
    m_sets.clear();
    for (auto& set : *sets) {
        set.name = uniqueName(set.name, std::nullopt);
        m_sets.push_back(std::move(set));
    }
    m_compiled.assign(m_sets.size(), nullptr);
    const auto index = settings.value(QStringLiteral("Highlight/default"), 0).toULongLong();
    m_default = index < m_sets.size() ? static_cast<std::size_t>(index) : 0;
    emit setsChanged();
    emit defaultChanged();
}

void HighlightSets::save(QSettings& settings) const
{
    settings.setValue(QStringLiteral("Highlight/sets"), toJson(m_sets));
    settings.setValue(QStringLiteral("Highlight/default"), static_cast<qulonglong>(m_default));
}

QByteArray HighlightSets::toJson(const std::vector<HighlightSet>& sets)
{
    QJsonArray array;
    for (const HighlightSet& set : sets) {
        QJsonArray rules;
        for (const HighlightRule& rule : set.rules) {
            rules.append(ruleToJson(rule));
        }
        array.append(
            QJsonObject{{QStringLiteral("name"), set.name}, {QStringLiteral("rules"), rules}});
    }
    const QJsonObject root{{QStringLiteral("format"), QString::fromLatin1(kFormat)},
                           {QStringLiteral("version"), kVersion},
                           {QStringLiteral("sets"), array}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::expected<std::vector<HighlightSet>, QString> HighlightSets::fromJson(const QByteArray& json)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(QStringLiteral("Invalid JSON at offset %1: %2")
                                   .arg(parseError.offset)
                                   .arg(parseError.errorString()));
    }
    if (!document.isObject() || document.object().value(QStringLiteral("format")).toString() !=
                                    QString::fromLatin1(kFormat)) {
        return std::unexpected(QStringLiteral("Not a highlight set file."));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(0) > kVersion) {
        return std::unexpected(
            QStringLiteral("The file was written by a newer version (format version %1).")
                .arg(root.value(QStringLiteral("version")).toInt()));
    }
    const QJsonValue setsValue = root.value(QStringLiteral("sets"));
    if (!setsValue.isArray()) {
        return std::unexpected(QStringLiteral("\"sets\" must be an array."));
    }
    std::vector<HighlightSet> sets;
    const QJsonArray setsArray = setsValue.toArray();
    for (qsizetype s = 0; s < setsArray.size(); ++s) {
        const QJsonValue setValue = setsArray.at(s);
        if (!setValue.isObject()) {
            return std::unexpected(QStringLiteral("Set %1: must be an object.").arg(s + 1));
        }
        const QJsonObject setObject = setValue.toObject();
        HighlightSet set;
        set.name = setObject.value(QStringLiteral("name")).toString();
        const QJsonValue rulesValue = setObject.value(QStringLiteral("rules"));
        if (!rulesValue.isArray()) {
            return std::unexpected(
                QStringLiteral("Set %1: \"rules\" must be an array.").arg(s + 1));
        }
        const QJsonArray rules = rulesValue.toArray();
        for (qsizetype r = 0; r < rules.size(); ++r) {
            auto rule = ruleFromJson(rules.at(r));
            if (!rule) {
                return std::unexpected(
                    QStringLiteral("Set %1, rule %2: %3").arg(s + 1).arg(r + 1).arg(rule.error()));
            }
            set.rules.push_back(std::move(*rule));
        }
        sets.push_back(std::move(set));
    }
    return sets;
}

}  // namespace lv::model
