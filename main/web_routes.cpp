#include "web_routes.h"

namespace rcctl {

void registerWebRoutes(WebServer& server, const WebRouteHandlers& h) {
    server.on("/", HTTP_GET, h.root);
    server.on("/generate_204", HTTP_GET, h.captiveRedirect);
    server.on("/gen_204", HTTP_GET, h.captiveRedirect);
    server.on("/hotspot-detect.html", HTTP_GET, h.captiveRedirect);
    server.on("/connecttest.txt", HTTP_GET, h.captiveRedirect);
    server.on("/ncsi.txt", HTTP_GET, h.captiveRedirect);
    server.on("/fwlink", HTTP_GET, h.captiveRedirect);

    server.on("/api/state", HTTP_GET, h.apiState);
    server.on("/api/activity", HTTP_GET, h.apiActivity);
    server.on("/api/inputs", HTTP_GET, h.apiInputs);
    server.on("/api/virtual_add", HTTP_POST, h.apiVirtualAdd);
    server.on("/api/virtual_update", HTTP_POST, h.apiVirtualUpdate);
    server.on("/api/virtual_delete", HTTP_POST, h.apiVirtualDelete);
    server.on("/api/output_add", HTTP_POST, h.apiOutputAdd);
    server.on("/api/output_update", HTTP_POST, h.apiOutputUpdate);
    server.on("/api/output_delete", HTTP_POST, h.apiOutputDelete);
    server.on("/api/preset_apply", HTTP_POST, h.apiPresetApply);
    server.on("/api/model_set_default", HTTP_POST, h.apiModelSetDefault);
    server.on("/api/model_create", HTTP_POST, h.apiModelCreate);
    server.on("/api/model_save_current", HTTP_POST, h.apiModelSaveCurrent);
    server.on("/api/model_revert", HTTP_POST, h.apiModelRevert);
    server.on("/api/model_delete", HTTP_POST, h.apiModelDelete);
    server.on("/api/ap_config_set", HTTP_POST, h.apiApConfigSet);
    server.on("/api/ap_config_apply_reboot", HTTP_POST, h.apiApConfigApplyReboot);
    server.on("/api/pairing_on", HTTP_POST, h.apiPairingOn);
    server.on("/api/pairing_off", HTTP_POST, h.apiPairingOff);
    server.on("/api/learn_detect", HTTP_POST, h.apiLearnDetect);
    server.onNotFound(h.notFound);
}

}  // namespace rcctl

