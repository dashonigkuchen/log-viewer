#include "model/line_highlighter.h"

#include "model/document.h"

namespace lv::model
{

LineHighlighter::LineHighlighter(QObject* parent)
    : QObject(parent)
{}

// NOLINTNEXTLINE(readability-make-member-function-const): connects this object
void LineHighlighter::watch(const Document& document)
{
    connect(&document, &Document::reset, this, &LineHighlighter::clearCache);
}

void LineHighlighter::setSets(const HighlightSets* sets)
{
    if (!m_sets.isNull()) {
        disconnect(m_sets, nullptr, this, nullptr);
    }
    m_sets = sets;
    if (sets != nullptr) {
        connect(sets, &HighlightSets::setChanged, this, [this](std::size_t index) {
            if (activeSet() == index) {
                clearCache();
            }
        });
        connect(sets, &HighlightSets::setsChanged, this, [this] {
            // Indexes may have moved: a chosen set that no longer exists falls back to the
            // default.
            if (m_chosen && *m_chosen >= m_sets->size()) {
                m_chosen.reset();
            }
            clearCache();
        });
        connect(sets, &HighlightSets::defaultChanged, this, [this] {
            if (!m_chosen) {
                clearCache();
            }
        });
    }
    clearCache();
}

void LineHighlighter::setActiveSet(std::optional<std::size_t> index)
{
    if (index && (m_sets.isNull() || *index >= m_sets->size())) {
        index.reset();
    }
    if (index == m_chosen) {
        return;
    }
    const auto before = activeSet();
    m_chosen = index;
    if (activeSet() != before) {
        clearCache();
    }
}

std::optional<std::size_t> LineHighlighter::activeSet() const
{
    if (m_sets.isNull() || m_sets->size() == 0) {
        return std::nullopt;
    }
    return m_chosen.value_or(m_sets->defaultIndex());
}

void LineHighlighter::clearCache()
{
    m_cache.clear();
    m_compiled.reset();
    m_scratch = core::MatchScratch{};
    emit changed();
}

LineHighlight LineHighlighter::highlight(core::LineNumber line, const QString& text,
                                         bool partial) const
{
    const auto active = activeSet();
    if (!active) {
        return {};
    }
    if (!partial) {
        if (const auto it = m_cache.find(line.get()); it != m_cache.end()) {
            return it->second;
        }
    }
    if (!m_compiled) {
        m_compiled = m_sets->compiled(*active);
    }
    LineHighlight result =
        m_compiled->empty() ? LineHighlight{} : m_compiled->evaluate(text, m_scratch);
    if (!partial) {
        if (m_cache.size() >= kCacheCapacity) {
            m_cache.clear();  // simple bound; painted lines are recomputed cheaply
        }
        m_cache.emplace(line.get(), result);
    }
    return result;
}

}  // namespace lv::model
