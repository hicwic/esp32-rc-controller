#pragma once

#include <WebServer.h>

// Mounts the filesystem partition that contains frontend assets.
bool initWebUiStorage();
// Streams /index.html from filesystem to the current HTTP client.
void streamWebUiPage(WebServer& server);
