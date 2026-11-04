#include "loggen.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#include <thread>
#include <utility>

namespace lv::loggen
{

namespace
{

struct ProfileEntry
{
    Profile profile;
    std::string_view name;
    std::uint64_t size;
};

constexpr std::array<ProfileEntry, 7> kProfiles{{
    {.profile = Profile::kSmall, .name = "small", .size = 50 * kMiB},
    {.profile = Profile::kLarge, .name = "large", .size = 1 * kGiB},
    {.profile = Profile::kHuge, .name = "huge", .size = 10 * kGiB},
    {.profile = Profile::kShort, .name = "short", .size = 1 * kGiB},
    {.profile = Profile::kLong, .name = "long", .size = 200 * kMiB},
    {.profile = Profile::kUtf16, .name = "utf16", .size = 200 * kMiB},
    {.profile = Profile::kCp1252, .name = "cp1252", .size = 50 * kMiB},
}};

// Line length ranges (inclusive, including the terminator, in characters).
constexpr std::uint64_t kTextLineMin = 110;  // mean 130
constexpr std::uint64_t kTextLineMax = 150;
constexpr std::uint64_t kShortLineMin = 12;  // mean 20
constexpr std::uint64_t kShortLineMax = 28;
constexpr std::uint64_t kLongLineMin = 3 * kMiB;  // mean 4 MiB
constexpr std::uint64_t kLongLineMax = 5 * kMiB;

// Level probabilities in 1/10000: ERROR 2%, WARN 5%, DEBUG 20%, INFO 73%.
constexpr std::uint64_t kErrorPermyriad = 200;
constexpr std::uint64_t kWarnPermyriad = 500;
constexpr std::uint64_t kDebugPermyriad = 2000;

// 2026-01-01T00:00:00Z in milliseconds since the Unix epoch.
constexpr std::uint64_t kStartTimeMs = 1767225600000ULL;
constexpr std::uint64_t kMaxStepMs = 40;

constexpr std::array<std::string_view, 12> kLoggers{
    "com.example.orders.OrderService", "com.example.orders.PaymentClient",
    "com.example.http.RequestHandler", "com.example.db.ConnectionPool",
    "com.example.cache.CacheManager",  "com.example.auth.TokenValidator",
    "com.example.jobs.Scheduler",      "com.example.inventory.StockSync",
    "com.example.mail.Notifier",       "com.example.api.Gateway",
    "com.example.metrics.Reporter",    "com.example.storage.BlobStore",
};

constexpr std::array<std::string_view, 8> kThreads{
    "main", "worker-1", "worker-2", "worker-3", "worker-4", "io-1", "io-2", "scheduler",
};

constexpr std::array<std::string_view, 6> kErrorLeads{
    "Request failed: timeout after 30000 ms", "Database error: connection reset by peer",
    "Unhandled exception in handler",         "Payment declined by provider",
    "Could not write file: disk full",        "Upstream returned status 503",
};

constexpr std::array<std::string_view, 5> kWarnLeads{
    "Slow response, close to timeout", "Retrying request, attempt 2 of 3",
    "Cache miss rate above threshold", "Connection pool nearly exhausted",
    "Deprecated API version used",
};

constexpr std::array<std::string_view, 6> kInfoLeads{
    "Processed order",       "User logged in",       "Request completed",
    "Scheduled job started", "Configuration loaded", "Stock level updated",
};

constexpr std::array<std::string_view, 4> kDebugLeads{
    "Cache lookup",
    "Entering method",
    "SQL statement prepared",
    "Header parsed",
};

constexpr std::array<std::string_view, 24> kWords{
    "request", "response", "status",  "client", "server",  "session", "token",  "value",
    "payload", "latency",  "retry",   "queue",  "message", "record",  "update", "account",
    "order",   "item",     "service", "node",   "backend", "result",  "cache",  "commit",
};

// German words with umlauts, UTF-8 in the source; converted to Latin-1/Windows-1252 at startup.
constexpr std::array<std::string_view, 10> kGermanWords{
    "Größe",     "Überprüfung", "Schlüssel", "gelöscht", "Änderung",
    "Prüfsumme", "Straße",      "Benutzer",  "Gebühr",   "Lösung",
};

constexpr std::array<std::string_view, 6> kKeys{
    "user", "requestId", "orderId", "durationMs", "bytes", "shard",
};

// Converts UTF-8 text whose code points are all <= U+00FF to Latin-1 (identical to
// Windows-1252 in that range for the letters used here).
std::string toLatin1(std::string_view utf8)
{
    std::string out;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto byte = static_cast<unsigned char>(utf8[i]);
        if (byte < 0x80U) {
            out += static_cast<char>(byte);
        } else {
            const auto next = static_cast<unsigned char>(utf8[i + 1]);
            const unsigned codePoint = ((byte & 0x1FU) << 6U) | (next & 0x3FU);
            out += static_cast<char>(static_cast<unsigned char>(codePoint));
            ++i;
        }
    }
    return out;
}

const std::array<std::string, kGermanWords.size()>& latin1GermanWords()
{
    static const auto words = [] {
        std::array<std::string, kGermanWords.size()> result;
        for (std::size_t i = 0; i < kGermanWords.size(); ++i) {
            result.at(i) = toLatin1(kGermanWords.at(i));
        }
        return result;
    }();
    return words;
}

template <typename Array>
std::string_view pick(Random& random, const Array& values)
{
    return values.at(static_cast<std::size_t>(random.below(values.size())));
}

void appendDigits(std::string& out, std::uint64_t value, int width)
{
    std::array<char, 20> buffer{};
    int length = 0;
    do {
        buffer.at(static_cast<std::size_t>(length++)) = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    for (int i = length; i < width; ++i) {
        out += '0';
    }
    while (length > 0) {
        out += buffer.at(static_cast<std::size_t>(--length));
    }
}

struct CivilDate
{
    std::int64_t year;
    std::uint64_t month;
    std::uint64_t day;
};

// Howard Hinnant's civil_from_days: days since 1970-01-01 to a proleptic Gregorian date.
CivilDate civilFromDays(std::int64_t days) noexcept
{
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto dayOfEra = static_cast<std::uint64_t>(days - (era * 146097));
    const std::uint64_t yearOfEra =
        (dayOfEra - (dayOfEra / 1460) + (dayOfEra / 36524) - (dayOfEra / 146096)) / 365;
    const std::uint64_t dayOfYear =
        dayOfEra - ((365 * yearOfEra) + (yearOfEra / 4) - (yearOfEra / 100));
    const std::uint64_t mp = ((5 * dayOfYear) + 2) / 153;
    const std::uint64_t day = dayOfYear - (((153 * mp) + 2) / 5) + 1;
    const std::uint64_t month = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year =
        static_cast<std::int64_t>(yearOfEra) + (era * 400) + (month <= 2 ? 1 : 0);
    return {.year = year, .month = month, .day = day};
}

void widenToUtf16Le(std::string_view latin1, std::string& out)
{
    for (const char c : latin1) {
        out += c;
        out += '\0';
    }
}

}  // namespace

std::optional<Profile> parseProfile(std::string_view name) noexcept
{
    for (const auto& entry : kProfiles) {
        if (entry.name == name) {
            return entry.profile;
        }
    }
    return std::nullopt;
}

std::string_view profileName(Profile profile) noexcept
{
    for (const auto& entry : kProfiles) {
        if (entry.profile == profile) {
            return entry.name;
        }
    }
    return "unknown";
}

std::uint64_t defaultSize(Profile profile) noexcept
{
    for (const auto& entry : kProfiles) {
        if (entry.profile == profile) {
            return entry.size;
        }
    }
    return 0;
}

std::optional<std::uint64_t> parseSize(std::string_view text) noexcept
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t multiplier = 1;
    switch (text.back()) {
    case 'k':
    case 'K':
        multiplier = kKiB;
        break;
    case 'm':
    case 'M':
        multiplier = kMiB;
        break;
    case 'g':
    case 'G':
        multiplier = kGiB;
        break;
    default:
        break;
    }
    if (multiplier != 1) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = (value * 10) + digit;
    }
    if (value > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return value * multiplier;
}

Random::Random(std::uint64_t seed) noexcept
{
    for (auto& word : m_state) {
        seed += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = seed;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        word = z ^ (z >> 31U);
    }
}

std::uint64_t Random::next() noexcept
{
    auto rotl = [](std::uint64_t x, unsigned k) {
        return (x << k) | (x >> (64U - k));
    };
    const std::uint64_t result = rotl(m_state[1] * 5, 7) * 9;
    const std::uint64_t t = m_state[1] << 17U;
    m_state[2] ^= m_state[0];
    m_state[3] ^= m_state[1];
    m_state[1] ^= m_state[2];
    m_state[0] ^= m_state[3];
    m_state[2] ^= t;
    m_state[3] = rotl(m_state[3], 45);
    return result;
}

std::uint64_t Random::below(std::uint64_t bound) noexcept
{
    const std::uint64_t threshold = (0 - bound) % bound;
    while (true) {
        const std::uint64_t value = next();
        if (value >= threshold) {
            return value % bound;
        }
    }
}

std::uint64_t Random::between(std::uint64_t low, std::uint64_t high) noexcept
{
    return low + below(high - low + 1);
}

Generator::Generator(Profile profile, std::uint64_t seed)
    : m_profile(profile)
    , m_random(seed)
    , m_latin1Words(profile == Profile::kCp1252 || profile == Profile::kUtf16)
    , m_timeMs(kStartTimeMs)
{}

std::string_view Generator::fileHeader() const noexcept
{
    if (m_profile == Profile::kUtf16) {
        return {"\xFF\xFE", 2};
    }
    return {};
}

void Generator::appendLine(std::string& out)
{
    switch (m_profile) {
    case Profile::kShort:
        buildShortLine(m_random.between(kShortLineMin, kShortLineMax) - 1);
        break;
    case Profile::kLong:
        buildTextLine(m_random.between(kLongLineMin, kLongLineMax) - 1);
        break;
    case Profile::kSmall:
    case Profile::kLarge:
    case Profile::kHuge:
    case Profile::kUtf16:
    case Profile::kCp1252:
        buildTextLine(m_random.between(kTextLineMin, kTextLineMax) - 1);
        break;
    }
    m_line += '\n';
    if (m_profile == Profile::kUtf16) {
        widenToUtf16Le(m_line, out);
    } else {
        out += m_line;
    }
}

Generator::Level Generator::pickLevel() noexcept
{
    const std::uint64_t roll = m_random.below(10000);
    if (roll < kErrorPermyriad) {
        return Level::kError;
    }
    if (roll < kErrorPermyriad + kWarnPermyriad) {
        return Level::kWarn;
    }
    if (roll < kErrorPermyriad + kWarnPermyriad + kDebugPermyriad) {
        return Level::kDebug;
    }
    return Level::kInfo;
}

void Generator::appendTimestamp()
{
    m_timeMs += m_random.below(kMaxStepMs + 1);
    const std::uint64_t msOfDay = m_timeMs % 86400000ULL;
    const CivilDate date = civilFromDays(static_cast<std::int64_t>(m_timeMs / 86400000ULL));
    appendDigits(m_line, static_cast<std::uint64_t>(date.year), 4);
    m_line += '-';
    appendDigits(m_line, date.month, 2);
    m_line += '-';
    appendDigits(m_line, date.day, 2);
    m_line += ' ';
    appendDigits(m_line, msOfDay / 3600000, 2);
    m_line += ':';
    appendDigits(m_line, (msOfDay / 60000) % 60, 2);
    m_line += ':';
    appendDigits(m_line, (msOfDay / 1000) % 60, 2);
    m_line += '.';
    appendDigits(m_line, msOfDay % 1000, 3);
}

void Generator::buildTextLine(std::uint64_t contentLength)
{
    // Format: "2026-01-01 00:00:00.123 INFO  [worker-1] com.example.Logger - message"
    m_line.clear();
    appendTimestamp();
    const Level level = pickLevel();
    switch (level) {
    case Level::kDebug:
        m_line += " DEBUG [";
        break;
    case Level::kInfo:
        m_line += " INFO  [";
        break;
    case Level::kWarn:
        m_line += " WARN  [";
        break;
    case Level::kError:
        m_line += " ERROR [";
        break;
    }
    m_line += pick(m_random, kThreads);
    m_line += "] ";
    m_line += pick(m_random, kLoggers);
    m_line += " - ";
    appendMessage(level, contentLength);
}

void Generator::appendMessage(Level level, std::uint64_t contentLength)
{
    switch (level) {
    case Level::kError:
        m_line += pick(m_random, kErrorLeads);
        break;
    case Level::kWarn:
        m_line += pick(m_random, kWarnLeads);
        break;
    case Level::kDebug:
        m_line += pick(m_random, kDebugLeads);
        break;
    case Level::kInfo:
        // 10% heartbeats, used by the boolean filter benchmark ("NOT heartbeat").
        m_line +=
            m_random.below(10) == 0 ? std::string_view{"heartbeat ok"} : pick(m_random, kInfoLeads);
        break;
    }
    while (m_line.size() < contentLength) {
        m_line += ' ';
        const std::uint64_t kind = m_random.below(10);
        if (kind < 2) {
            m_line += pick(m_random, kKeys);
            m_line += '=';
            appendDigits(m_line, m_random.below(100000), 1);
        } else if (kind < 4 && m_latin1Words) {
            m_line += latin1GermanWords().at(
                static_cast<std::size_t>(m_random.below(kGermanWords.size())));
        } else {
            m_line += pick(m_random, kWords);
        }
    }
    m_line.resize(static_cast<std::size_t>(contentLength));
}

void Generator::buildShortLine(std::uint64_t contentLength)
{
    // Format: "I 000042 tick tick ..."
    m_line.clear();
    switch (pickLevel()) {
    case Level::kDebug:
        m_line += 'D';
        break;
    case Level::kInfo:
        m_line += 'I';
        break;
    case Level::kWarn:
        m_line += 'W';
        break;
    case Level::kError:
        m_line += 'E';
        break;
    }
    m_line += ' ';
    appendDigits(m_line, m_counter++ % 1000000, 6);
    while (m_line.size() < contentLength) {
        m_line += " tick";
    }
    m_line.resize(static_cast<std::size_t>(contentLength));
}

namespace
{

class OutputFile
{
public:
    OutputFile(std::filesystem::path path, std::string_view header)
        : m_path(std::move(path))
        , m_header(header)
    {}

    bool open(bool append, WriteResult& result)
    {
        m_stream.open(m_path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!m_stream) {
            result.error = "cannot open " + m_path.string();
            return false;
        }
        std::error_code ec;
        const auto existing = append ? std::filesystem::file_size(m_path, ec) : 0;
        m_fileBytes = ec ? 0 : existing;
        if (m_fileBytes == 0) {
            return write(m_header, result);
        }
        return true;
    }

    bool write(std::string_view data, WriteResult& result)
    {
        m_stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        m_stream.flush();
        if (!m_stream) {
            result.error = "write failed: " + m_path.string();
            return false;
        }
        m_fileBytes += data.size();
        result.bytes += data.size();
        return true;
    }

    // Rotates or truncates when the file reached a threshold.
    bool applyLimits(const WriteOptions& options, WriteResult& result)
    {
        if (options.rotateEveryBytes != 0 && m_fileBytes >= options.rotateEveryBytes) {
            m_stream.close();
            std::filesystem::path rotated = m_path;
            rotated += ".1";
            std::error_code ec;
            std::filesystem::rename(m_path, rotated, ec);
            if (ec) {
                result.error = "rotate failed: " + ec.message();
                return false;
            }
            ++result.rotations;
            return open(false, result);
        }
        if (options.truncateEveryBytes != 0 && m_fileBytes >= options.truncateEveryBytes) {
            m_stream.close();
            ++result.truncations;
            return open(false, result);
        }
        return true;
    }

    [[nodiscard]] std::uint64_t fileBytes() const noexcept { return m_fileBytes; }

private:
    std::filesystem::path m_path;
    std::string_view m_header;
    std::ofstream m_stream;
    std::uint64_t m_fileBytes = 0;
};

[[nodiscard]] std::uint64_t nearestLimit(const WriteOptions& options, std::uint64_t fileBytes)
{
    std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    for (const std::uint64_t threshold : {options.rotateEveryBytes, options.truncateEveryBytes}) {
        if (threshold != 0) {
            limit = std::min(limit, threshold > fileBytes ? threshold - fileBytes : 0);
        }
    }
    return limit;
}

}  // namespace

WriteResult writeLog(const WriteOptions& options)
{
    WriteResult result;
    Generator generator(options.profile, options.seed);
    OutputFile file(options.out, generator.fileHeader());
    const bool live = options.appendRateBytesPerSecond != 0;
    if (!file.open(live, result)) {
        return result;
    }

    constexpr std::uint64_t kFastChunk = 1 * kMiB;
    constexpr std::uint64_t kLiveTicksPerSecond = 100;  // one write every ~10 ms
    const std::uint64_t chunkTarget =
        live ? std::max<std::uint64_t>(1, options.appendRateBytesPerSecond / kLiveTicksPerSecond)
             : kFastChunk;
    const std::uint64_t sizeTarget =
        options.sizeBytes != 0 ? options.sizeBytes : defaultSize(options.profile);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(options.durationMs);
    std::uint64_t paced = 0;  // bytes written in live mode, for rate pacing
    std::string chunk;

    while (true) {
        if (live ? Clock::now() >= deadline : result.bytes >= sizeTarget) {
            break;
        }
        chunk.clear();
        const std::uint64_t limit = nearestLimit(options, file.fileBytes());
        while (chunk.size() < chunkTarget && chunk.size() < limit &&
               (live || result.bytes + chunk.size() < sizeTarget)) {
            generator.appendLine(chunk);
            ++result.lines;
        }
        if (!file.write(chunk, result) || !file.applyLimits(options, result)) {
            return result;
        }
        if (live) {
            paced += chunk.size();
            const auto due = start + std::chrono::microseconds(paced * 1000000 /
                                                               options.appendRateBytesPerSecond);
            std::this_thread::sleep_until(std::min(due, deadline));
        }
    }
    return result;
}

}  // namespace lv::loggen
