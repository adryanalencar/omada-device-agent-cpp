#include "openomada/protocol/json.hpp"

#include <string>

namespace openomada::protocol {

JsonDocument::~JsonDocument() {
    if (object_ != nullptr) {
        json_object_put(object_);
        object_ = nullptr;
    }
}

JsonDocument::JsonDocument(JsonDocument&& other) noexcept : object_(other.object_) {
    other.object_ = nullptr;
}

JsonDocument& JsonDocument::operator=(JsonDocument&& other) noexcept {
    if (this != &other) {
        if (object_ != nullptr) {
            json_object_put(object_);
        }
        object_ = other.object_;
        other.object_ = nullptr;
    }
    return *this;
}

JsonDocument JsonDocument::parse(std::string_view text) noexcept {
    std::string owned(text);
    json_tokener* tokener = json_tokener_new();
    if (tokener == nullptr) {
        return JsonDocument();
    }
    json_object* parsed = json_tokener_parse_ex(tokener, owned.data(), static_cast<int>(owned.size()));
    const json_tokener_error error = json_tokener_get_error(tokener);
    json_tokener_free(tokener);
    if (error != json_tokener_success || parsed == nullptr || !json_object_is_type(parsed, json_type_object)) {
        if (parsed != nullptr) {
            json_object_put(parsed);
        }
        return JsonDocument();
    }
    return JsonDocument(parsed);
}

std::string JsonDocument::dump() const {
    if (object_ == nullptr) {
        return {};
    }
    const char* rendered = json_object_to_json_string_ext(object_, JSON_C_TO_STRING_PLAIN);
    return rendered == nullptr ? std::string() : std::string(rendered);
}

json_object* object_member(json_object* object, const char* key) noexcept {
    if (object == nullptr || key == nullptr || !json_object_is_type(object, json_type_object)) {
        return nullptr;
    }
    json_object* value = nullptr;
    if (!json_object_object_get_ex(object, key, &value)) {
        return nullptr;
    }
    return value;
}

std::optional<std::string> json_string(json_object* object) noexcept {
    if (object == nullptr || !json_object_is_type(object, json_type_string)) {
        return std::nullopt;
    }
    const char* value = json_object_get_string(object);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

std::optional<std::int64_t> json_int(json_object* object) noexcept {
    if (object == nullptr) {
        return std::nullopt;
    }
    if (!json_object_is_type(object, json_type_int) && !json_object_is_type(object, json_type_double)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(json_object_get_int64(object));
}

std::optional<std::uint32_t> ecsp_header_type(json_object* message) noexcept {
    auto* header = object_member(message, "header");
    auto value = json_int(object_member(header, "type"));
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::int32_t ecsp_header_error(json_object* message) noexcept {
    auto* header = object_member(message, "header");
    auto value = json_int(object_member(header, "error"));
    if (!value.has_value()) {
        return 0;
    }
    return static_cast<std::int32_t>(*value);
}

std::optional<std::uint32_t> ecsp_header_seq(json_object* message) noexcept {
    auto* header = object_member(message, "header");
    auto value = json_int(object_member(header, "seq"));
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

json_object* ecsp_body(json_object* message) noexcept {
    auto* body = object_member(message, "body");
    if (body == nullptr || !json_object_is_type(body, json_type_object)) {
        return nullptr;
    }
    return body;
}

} // namespace openomada::protocol

