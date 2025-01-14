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

// Global variables
static char *g_serial = NULL;
static char *g_uid = NULL;

// Statistics variables
static int stat_eventServiceXMLVisits = 0;
static int stat_metaServiceXMLVisits = 0;

// Placeholder response
const char *g_wemo_eventService = "<scpd xmlns=\"urn:Belkin:service-1-0\"></scpd>\r\n\r\n";
const char *g_wemo_metaService = "<scpd xmlns=\"urn:Belkin:service-1-0\"></scpd>\r\n\r\n";

// Event Service Handler
static int WEMO_EventService(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_eventService);
    poststr(request, NULL);
    stat_eventServiceXMLVisits++;
    return 0;
}

// Meta Info Service Handler
static int WEMO_MetaInfoService(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_metaService);
    poststr(request, NULL);
    stat_metaServiceXMLVisits++;
    return 0;
}

// Basic Event Handler
static int WEMO_BasicEvent1(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, "BasicEvent1 Response");
    poststr(request, NULL);
    return 0;
}

// Setup Handler
static int WEMO_Setup(http_request_t *request) {
    http_setup(request, httpMimeTypeXML);
    poststr(request, "<Setup Response>");
    poststr(request, NULL);
    return 0;
}

// Initialization Function
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
