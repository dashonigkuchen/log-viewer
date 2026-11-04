#include "core/util/strong_types.h"

#include <array>
#include <charconv>
#include <string_view>

namespace lv::core
{

namespace
{

// Appends a decimal number. Avoids `literal + std::to_string(...)`, which triggers a GCC 14
// -Wstringop-overflow false positive under LTO.
void appendNumber(std::string& out, std::uint64_t value)
{
    std::array<char, 20> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), result.ptr);
}

std::string format(std::string_view prefix, std::uint64_t value, std::string_view suffix)
{
    std::string text(prefix);
    appendNumber(text, value);
    text += suffix;
    return text;
}

}  // namespace

std::string toString(LineNumber value)
{
    return format("line#", value.get(), "");
}

std::string toString(LineCount value)
{
    return format("", value.get(), " lines");
}

std::string toString(ByteOffset value)
{
    return format("@", value.get(), "");
}

std::string toString(ByteRange value)
{
    std::string text = format("[", value.begin().get(), ", ");
    appendNumber(text, value.end().get());
    text += ')';
    return text;
}

std::ostream& operator<<(std::ostream& out, LineNumber value)
{
    return out << toString(value);
}

std::ostream& operator<<(std::ostream& out, LineCount value)
{
    return out << toString(value);
}

std::ostream& operator<<(std::ostream& out, ByteOffset value)
{
    return out << toString(value);
}

std::ostream& operator<<(std::ostream& out, ByteRange value)
{
    return out << toString(value);
}

}  // namespace lv::core
