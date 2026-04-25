#include "json_utils.h"

namespace rcctl {

JsonWriter::JsonWriter() : depth_(0), expectingValue_(false) {}

void JsonWriter::beginObject() {
    beforeValue();
    out_ += "{";
    stack_[depth_++] = {'o', true};
    expectingValue_ = false;
}

void JsonWriter::endObject() {
    out_ += "}";
    if (depth_ > 0) {
        --depth_;
    }
    expectingValue_ = false;
}

void JsonWriter::beginArray() {
    beforeValue();
    out_ += "[";
    stack_[depth_++] = {'a', true};
    expectingValue_ = false;
}

void JsonWriter::endArray() {
    out_ += "]";
    if (depth_ > 0) {
        --depth_;
    }
    expectingValue_ = false;
}

void JsonWriter::key(const char* key) {
    if (depth_ <= 0 || stack_[depth_ - 1].type != 'o') {
        return;
    }
    if (!stack_[depth_ - 1].first) {
        out_ += ",";
    }
    stack_[depth_ - 1].first = false;
    appendEscapedString(String(key));
    out_ += ":";
    expectingValue_ = true;
}

void JsonWriter::value(const String& value) {
    beforeValue();
    appendEscapedString(value);
    expectingValue_ = false;
}

void JsonWriter::value(const char* text) {
    value(String(text ? text : ""));
}

void JsonWriter::value(int value) {
    beforeValue();
    out_ += String(value);
    expectingValue_ = false;
}

void JsonWriter::value(float value, int decimals) {
    beforeValue();
    out_ += String(value, decimals);
    expectingValue_ = false;
}

void JsonWriter::value(bool value) {
    beforeValue();
    out_ += value ? "true" : "false";
    expectingValue_ = false;
}

void JsonWriter::rawValue(const String& rawJson) {
    beforeValue();
    out_ += rawJson;
    expectingValue_ = false;
}

const String& JsonWriter::str() const {
    return out_;
}

String JsonWriter::escape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
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
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    const char hex[] = "0123456789abcdef";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

void JsonWriter::beforeValue() {
    if (expectingValue_) {
        return;
    }
    if (depth_ <= 0) {
        return;
    }
    Frame& f = stack_[depth_ - 1];
    if (f.type == 'a') {
        // Array items are comma-separated; objects are handled in key().
        if (!f.first) {
            out_ += ",";
        }
        f.first = false;
    }
}

void JsonWriter::appendEscapedString(const String& value) {
    out_ += "\"";
    out_ += escape(value);
    out_ += "\"";
}

}  // namespace rcctl
