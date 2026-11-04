#pragma once

#include "loggen/loggen.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lv::bench
{

// Directory for generated benchmark data: $LV_BENCH_DATA, or <build dir>/bench-data.
[[nodiscard]] std::filesystem::path dataDirectory();

struct BenchFile
{
    std::filesystem::path path;
    std::uint64_t sizeBytes = 0;
    std::string error;  // empty on success
};

// Returns the cached data file of a profile, generating it with loggen first if it does not
// exist yet. Generation writes to a ".partial" file and renames it, so an interrupted run never
// leaves a truncated file behind.
[[nodiscard]] BenchFile ensureData(lv::loggen::Profile profile, std::uint64_t seed = 1);

// Profiles to benchmark: $LV_BENCH_PROFILES (comma-separated, e.g. "small,short"), default
// "small". Unknown names are reported in `errors`.
[[nodiscard]] std::vector<lv::loggen::Profile> selectedProfiles(std::vector<std::string>& errors);

}  // namespace lv::bench
