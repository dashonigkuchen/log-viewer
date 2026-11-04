#include "bench_data.h"

#include <cstdlib>
#include <string_view>
#include <system_error>

namespace lv::bench
{

namespace
{

std::string environment(const char* name)
{
    const char* value = std::getenv(name);  // NOLINT(concurrency-mt-unsafe): read once at startup
    return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

std::filesystem::path dataDirectory()
{
    const std::string overrideDir = environment("LV_BENCH_DATA");
    return overrideDir.empty() ? std::filesystem::path(LV_BENCH_DEFAULT_DATA_DIR)
                               : std::filesystem::path(overrideDir);
}

BenchFile ensureData(lv::loggen::Profile profile, std::uint64_t seed)
{
    BenchFile file;
    const std::filesystem::path directory = dataDirectory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        file.error = "cannot create " + directory.string() + ": " + ec.message();
        return file;
    }
    file.path = directory / (std::string(lv::loggen::profileName(profile)) + "-seed" +
                             std::to_string(seed) + ".log");
    if (!std::filesystem::exists(file.path, ec)) {
        std::filesystem::path partial = file.path;
        partial += ".partial";
        lv::loggen::WriteOptions options;
        options.profile = profile;
        options.seed = seed;
        options.out = partial;
        const lv::loggen::WriteResult result = lv::loggen::writeLog(options);
        if (!result.ok()) {
            file.error = "generating " + partial.string() + " failed: " + result.error;
            return file;
        }
        std::filesystem::rename(partial, file.path, ec);
        if (ec) {
            file.error = "cannot rename " + partial.string() + ": " + ec.message();
            return file;
        }
    }
    file.sizeBytes = std::filesystem::file_size(file.path, ec);
    if (ec) {
        file.error = "cannot stat " + file.path.string() + ": " + ec.message();
    }
    return file;
}

std::vector<lv::loggen::Profile> selectedProfiles(std::vector<std::string>& errors)
{
    std::string list = environment("LV_BENCH_PROFILES");
    if (list.empty()) {
        list = "small";
    }
    std::vector<lv::loggen::Profile> profiles;
    std::string_view rest = list;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view name = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
        if (name.empty()) {
            continue;
        }
        if (const auto profile = lv::loggen::parseProfile(name)) {
            profiles.push_back(*profile);
        } else {
            errors.push_back("unknown profile in LV_BENCH_PROFILES: " + std::string(name));
        }
    }
    return profiles;
}

}  // namespace lv::bench
