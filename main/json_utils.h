#pragma once

#include <Arduino.h>

namespace rcctl {

class JsonWriter {
public:
    JsonWriter();

    // Structural API.
    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    // Adds an object key, then expects a matching value().
    void key(const char* key);

    // Typed value emitters.
    void value(const String& value);
    void value(const char* value);
    void value(int value);
    void value(float value, int decimals = 3);
    void value(bool value);
    // Emits already-serialized JSON (use with caution).
    void rawValue(const String& rawJson);

    // Returns the full serialized JSON payload.
    const String& str() const;

    // Escapes string content to JSON-safe format.
    static String escape(const String& value);

private:
    struct Frame {
        char type;
        bool first;
    };

    void beforeValue();
    void appendEscapedString(const String& value);

    String out_;
    Frame stack_[16];
    int depth_;
    bool expectingValue_;
};

}  // namespace rcctl
