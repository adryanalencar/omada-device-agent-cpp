#include "openomada/protocol/ecsp_message.hpp"

namespace openomada::protocol {

std::string json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char raw : value) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20U) {
                static constexpr char kHex[] = "0123456789ABCDEF";
                out += "\\u00";
                out.push_back(kHex[(ch >> 4) & 0x0FU]);
                out.push_back(kHex[ch & 0x0FU]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

std::string build_message_json(
    const EcspHeader& header,
    std::string_view body_json,
    bool include_body
) {
    std::string out;
    out.reserve(192 + body_json.size());
    out += "{\"header\":{";
    bool need_comma = false;
    const auto comma = [&out, &need_comma]() {
        if (need_comma) {
            out.push_back(',');
        }
        need_comma = true;
    };

    if (header.seq.has_value()) {
        comma();
        out += "\"seq\":";
        out += std::to_string(*header.seq);
    }
    comma();
    out += "\"version\":\"";
    out += json_escape(header.version);
    out += "\"";
    comma();
    out += "\"verCap\":";
    out += std::to_string(header.ver_cap);
    comma();
    out += "\"device\":\"";
    out += json_escape(header.device);
    out += "\"";
    comma();
    out += "\"mac\":\"";
    out += header.mac.omada();
    out += "\"";
    comma();
    out += "\"type\":";
    out += std::to_string(to_underlying(header.type));
    comma();
    out += "\"error\":";
    out += std::to_string(header.error);
    if (!header.dest.empty()) {
        comma();
        out += "\"dest\":\"";
        out += json_escape(header.dest);
        out += "\"";
    }
    if (header.timestamp.has_value()) {
        comma();
        out += "\"timestamp\":";
        out += std::to_string(*header.timestamp);
    }
    out += "}";
    if (include_body) {
        out += ",\"body\":";
        out += body_json.empty() ? "{}" : std::string(body_json);
    }
    out += "}";
    return out;
}

} // namespace openomada::protocol
