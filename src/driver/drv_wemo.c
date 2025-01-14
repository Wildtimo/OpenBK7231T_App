#include "../new_common.h"
#include "../new_pins.h"
#include "../new_cfg.h"
#include "../cmnds/cmd_public.h"
#include "../mqtt/new_mqtt.h"
#include "../logging/logging.h"
#include "../hal/hal_pins.h"
#include "../hal/hal_wifi.h"
#include "drv_public.h"
#include "drv_local.h"
#include "drv_ssdp.h"
#include "../httpserver/new_http.h"

static const char *g_wemo_response_1 =
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
    "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
    "<s:Body>";

const char *g_wemo_eventService = "<scpd xmlns=\"urn:Belkin:service-1-0\">"
                                  // (your event service XML content here)
                                  "</scpd>\r\n\r\n";

const char *g_wemo_metaService = "<scpd xmlns=\"urn:Belkin:service-1-0\">"
                                 // (your meta service XML content here)
                                 "</scpd>\r\n\r\n";

static int WEMO_EventService(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_eventService);
    poststr(request, NULL);
    stat_eventServiceXMLVisits++;
    return 0;
}

static int WEMO_MetaInfoService(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_metaService);
    poststr(request, NULL);
    stat_metaServiceXMLVisits++;
    return 0;
}

void WEMO_Init() {
    char uid[64];
    char serial[32];
    unsigned char mac[8];

    WiFI_GetMacAddress((char *)mac);
    snprintf(serial, sizeof(serial), "201612%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    snprintf(uid, sizeof(uid), "Socket-1_0-%s", serial);

    g_serial = strdup(serial);
    g_uid = strdup(uid);

    HTTP_RegisterCallback("/upnp/control/basicevent1", HTTP_POST, WEMO_BasicEvent1, 0);
    HTTP_RegisterCallback("/eventservice.xml", HTTP_GET, WEMO_EventService, 0);
    HTTP_RegisterCallback("/metainfoservice.xml", HTTP_GET, WEMO_MetaInfoService, 0);
    HTTP_RegisterCallback("/setup.xml", HTTP_GET, WEMO_Setup, 0);
}
