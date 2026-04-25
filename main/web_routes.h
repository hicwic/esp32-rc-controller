#pragma once

#include <WebServer.h>

namespace rcctl {

struct WebRouteHandlers {
    // Captive portal + root UI.
    WebServer::THandlerFunction root;
    WebServer::THandlerFunction captiveRedirect;
    // Read-only state endpoints.
    WebServer::THandlerFunction apiState;
    WebServer::THandlerFunction apiActivity;
    WebServer::THandlerFunction apiInputs;
    // Model mutation endpoints.
    WebServer::THandlerFunction apiVirtualAdd;
    WebServer::THandlerFunction apiVirtualUpdate;
    WebServer::THandlerFunction apiVirtualDelete;
    WebServer::THandlerFunction apiOutputAdd;
    WebServer::THandlerFunction apiOutputUpdate;
    WebServer::THandlerFunction apiOutputDelete;
    WebServer::THandlerFunction apiPresetApply;
    WebServer::THandlerFunction apiModelSetDefault;
    WebServer::THandlerFunction apiModelCreate;
    WebServer::THandlerFunction apiModelSaveCurrent;
    WebServer::THandlerFunction apiModelRevert;
    WebServer::THandlerFunction apiModelDelete;
    WebServer::THandlerFunction apiApConfigSet;
    WebServer::THandlerFunction apiApConfigApplyReboot;
    WebServer::THandlerFunction apiPairingOn;
    WebServer::THandlerFunction apiPairingOff;
    WebServer::THandlerFunction apiLearnDetect;
    // 404 fallback.
    WebServer::THandlerFunction notFound;
};

// Centralizes route registration to keep setupWebUi() compact and testable.
void registerWebRoutes(WebServer& server, const WebRouteHandlers& handlers);

}  // namespace rcctl
