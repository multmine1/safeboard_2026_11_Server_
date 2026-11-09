#include "readers.hpp"

#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace skeleton::input {

[[noreturn]] void Fail(const std::string& context, const std::string& message) {
    throw ConfigurationError(context + ": " + message);
}

void CheckFields(const Json& object, std::initializer_list<std::string_view> fields,
                 const std::string& context) {
    if (!object.is_object()) Fail(context, "expected an object");
    for (const auto& [key, value] : object.items()) {
        if (std::find(fields.begin(), fields.end(), key) == fields.end())
            Fail(context + "/" + key, "unknown field");
    }
}

const Json& Require(const Json& object, const std::string& key,
                    const std::string& context) {
    if (!object.contains(key)) Fail(context + "/" + key, "required field is missing");
    return object.at(key);
}

std::string ReadString(const Json& object, const std::string& key,
                       const std::string& context) {
    const auto& value = Require(object, key, context);
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        Fail(context + "/" + key, "expected a nonempty string");
    return value.get<std::string>();
}

std::uint64_t ReadPositive(const Json& object, const std::string& key,
                           const std::string& context, std::uint64_t maximum) {
    const auto& value = Require(object, key, context);
    if (!value.is_number_integer() ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() <= 0))
        Fail(context + "/" + key, "expected a positive integer");
    const auto result = value.get<std::uint64_t>();
    if (result == 0 || result > maximum)
        Fail(context + "/" + key, "value is outside the supported range");
    return result;
}

std::string Lowercase(std::string value) {
    for (auto& character : value)
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    return value;
}

std::string ReadUuid(const Json& object, const std::string& key,
                     const std::string& context) {
    static const std::regex format(
        "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    auto value = Lowercase(ReadString(object, key, context));
    if (!std::regex_match(value, format)) Fail(context + "/" + key, "expected a UUID");
    return value;
}

std::string ReadName(const Json& object, const std::string& key,
                     const std::string& context) {
    static const std::regex label("[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?");
    auto value = Lowercase(ReadString(object, key, context));
    if (!std::regex_match(value, label))
        Fail(context + "/" + key, "expected a DNS label of at most 63 characters");
    return value;
}

std::vector<std::string> ReadStringArray(const Json& object, const std::string& key,
                                        const std::string& context) {
    const auto& array = Require(object, key, context);
    if (!array.is_array()) Fail(context + "/" + key, "expected an array");
    std::vector<std::string> result;
    for (const auto& value : array) {
        if (!value.is_string() || value.get_ref<const std::string&>().empty())
            Fail(context + "/" + key, "expected nonempty strings");
        result.push_back(value.get<std::string>());
    }
    return result;
}

Json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) Fail(path.string(), "cannot read file");
    try {
        return Json::parse(stream);
    } catch (const Json::exception& error) {
        Fail(path.string(), error.what());
    }
}

}  // namespace skeleton::input
