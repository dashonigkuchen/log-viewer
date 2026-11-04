#include "loggen.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace
{

constexpr std::string_view kUsage =
    R"(Usage: loggen --profile NAME --out FILE [options]

Writes a deterministic synthetic log file (docs/PERFORMANCE.md section 2).

Profiles: small (50M), large (1G), huge (10G), short (1G), long (200M), utf16 (200M), cp1252 (50M)

Options:
  --profile NAME          Profile to generate (required)
  --out FILE              Output file (required)
  --seed N                PRNG seed (default 1); the same seed gives identical output
  --size SIZE             Bytes to write, e.g. 4096, 64K, 5M, 1G (default: profile size)
  --append-rate MBPS      Live mode: append at MBPS MiB/s, flushing every write
  --duration SECONDS      Live mode duration (required with --append-rate)
  --rotate-every MB       Rename FILE to FILE.1 and start a new file every MB MiB
  --truncate-every MB     Truncate FILE to zero every MB MiB
  -h, --help              Show this help
)";

int fail(std::string_view message)
{
    std::cerr << "loggen: " << message << "\n\n" << kUsage;
    return 2;
}

std::optional<std::uint64_t> parseNumber(std::string_view text)
{
    if (text.empty() || text.back() < '0' || text.back() > '9') {
        return std::nullopt;
    }
    return lv::loggen::parseSize(text);
}

}  // namespace

int main(int argc, char* argv[])
{
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    lv::loggen::WriteOptions options;
    std::optional<lv::loggen::Profile> profile;
    std::optional<std::uint64_t> durationSeconds;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        }
        if (i + 1 >= args.size()) {
            return fail("missing value for " + std::string(arg));
        }
        const std::string_view value = args[++i];
        if (arg == "--profile") {
            profile = lv::loggen::parseProfile(value);
            if (!profile) {
                return fail("unknown profile '" + std::string(value) + "'");
            }
        } else if (arg == "--out") {
            options.out = std::filesystem::path(std::string(value));
        } else if (arg == "--seed" || arg == "--append-rate" || arg == "--duration" ||
                   arg == "--rotate-every" || arg == "--truncate-every") {
            const auto number = parseNumber(value);
            if (!number) {
                return fail("invalid number for " + std::string(arg));
            }
            if (arg == "--seed") {
                options.seed = *number;
            } else if (arg == "--append-rate") {
                options.appendRateBytesPerSecond = *number * lv::loggen::kMiB;
            } else if (arg == "--duration") {
                durationSeconds = number;
            } else if (arg == "--rotate-every") {
                options.rotateEveryBytes = *number * lv::loggen::kMiB;
            } else {
                options.truncateEveryBytes = *number * lv::loggen::kMiB;
            }
        } else if (arg == "--size") {
            const auto size = lv::loggen::parseSize(value);
            if (!size || *size == 0) {
                return fail("invalid size '" + std::string(value) + "'");
            }
            options.sizeBytes = *size;
        } else {
            return fail("unknown option " + std::string(arg));
        }
    }

    if (!profile) {
        return fail("--profile is required");
    }
    if (options.out.empty()) {
        return fail("--out is required");
    }
    if ((options.appendRateBytesPerSecond != 0) != durationSeconds.has_value()) {
        return fail("--append-rate and --duration must be used together");
    }
    options.profile = *profile;
    options.durationMs = durationSeconds.value_or(0) * 1000;

    const lv::loggen::WriteResult result = lv::loggen::writeLog(options);
    if (!result.ok()) {
        std::cerr << "loggen: " << result.error << '\n';
        return 1;
    }
    std::cout << "Wrote " << result.bytes << " bytes, " << result.lines << " lines ("
              << lv::loggen::profileName(options.profile) << ", seed " << options.seed << ")";
    if (result.rotations != 0) {
        std::cout << ", " << result.rotations << " rotations";
    }
    if (result.truncations != 0) {
        std::cout << ", " << result.truncations << " truncations";
    }
    std::cout << '\n';
    return 0;
}
