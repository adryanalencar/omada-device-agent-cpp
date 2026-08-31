#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <json-c/json.h>

namespace openomada::protocol {

class JsonDocument {
public:
    JsonDocument() noexcept = default;
    explicit JsonDocument(json_object* object) noexcept : object_(object) {}
    ~JsonDocument();

    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(JsonDocument&& other) noexcept;

    static JsonDocument parse(std::string_view text) noexcept;

    bool valid() const noexcept { return object_ != nullptr; }
    json_object* get() const noexcept { return object_; }
    std::string dump() const;

private:
    json_object* object_{nullptr};
};

json_object* object_member(json_object* object, const char* key) noexcept;
std::optional<std::string> json_string(json_object* object) noexcept;
std::optional<std::int64_t> json_int(json_object* object) noexcept;

std::optional<std::uint32_t> ecsp_header_type(json_object* message) noexcept;
std::int32_t ecsp_header_error(json_object* message) noexcept;
std::optional<std::uint32_t> ecsp_header_seq(json_object* message) noexcept;
json_object* ecsp_body(json_object* message) noexcept;

} // namespace openomada::protocol

