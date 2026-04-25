#include "web_ui.h"

#include <esp_spiffs.h>
#include <sys/stat.h>
#include <stdio.h>

namespace {

constexpr const char* kUiPath = "/index.html";
constexpr const char* kBasePath = "/spiffs";
constexpr const char* kPartitionLabel = "spiffs";
bool g_uiFsMounted = false;

String absoluteUiPath() {
    String p = kBasePath;
    p += kUiPath;
    return p;
}

}  // namespace

bool initWebUiStorage() {
    if (g_uiFsMounted) {
        return true;
    }
    // Use ESP-IDF SPIFFS mount directly so this path works in pure espidf builds.
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = kBasePath;
    conf.partition_label = kPartitionLabel;
    conf.max_files = 6;
    conf.format_if_mount_failed = true;
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    g_uiFsMounted = (err == ESP_OK);
    return g_uiFsMounted;
}

void streamWebUiPage(WebServer& server) {
    if (!g_uiFsMounted && !initWebUiStorage()) {
        server.send(500, "text/plain", "SPIFFS mount failed.");
        return;
    }

    const String path = absoluteUiPath();
    struct stat st = {};
    if (stat(path.c_str(), &st) != 0 || st.st_size <= 0) {
        server.send(500, "text/plain", "Missing UI asset /index.html. Upload filesystem image.");
        return;
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        server.send(500, "text/plain", "Failed to open UI asset.");
        return;
    }

    server.sendHeader("Cache-Control", "no-cache");
    server.setContentLength(static_cast<size_t>(st.st_size));
    server.send(200, "text/html; charset=utf-8", "");

    char buf[1024];
    // Stream in chunks to avoid building a large intermediate String in RAM.
    while (true) {
        const size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0) {
            break;
        }
        server.sendContent(buf, n);
        yield();
    }
    fclose(f);
}
