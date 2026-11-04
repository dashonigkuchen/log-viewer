#pragma once

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace lv::test
{

// A unique temporary directory, removed recursively when the object is destroyed.
class TempDir
{
public:
    TempDir()
        : m_path(std::filesystem::temp_directory_path() /
                 ("lv-test-" + std::to_string(std::random_device{}()) + "-" +
                  std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_path);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::permissions(m_path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ec);
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    [[nodiscard]] std::filesystem::path operator/(const std::string& name) const
    {
        return m_path / name;
    }

private:
    std::filesystem::path m_path;
};

}  // namespace lv::test
