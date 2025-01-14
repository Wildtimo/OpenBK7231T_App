#include "../new_common.h"
#include "../new_pins.h"
#include "../new_cfg.h"
// Commands register, execution API and cmd tokenizer
#include "../cmnds/cmd_public.h"
#include "../mqtt/new_mqtt.h"
#include "../logging/logging.h"
#include "../hal/hal_pins.h"
#include "../hal/hal_wifi.h"
#include "drv_public.h"
#include "drv_local.h"
#include "drv_ssdp.h"
#include "../httpserver/new_http.h"

// Wemo driver for Alexa, requiring btsimonh's SSDP to work
// Based on Tasmota approach
// Supports multiple relays.

static const char *g_wemo_setup_1 =
"<?xml version=\"1.0\"?>"
"<root xmlns=\"urn:Belkin:device-1-0\">"
"<device>"
"<deviceType>urn:Belkin:device:controllee:1</deviceType>"
"<friendlyName>";
static const char *g_wemo_setup_2 =
"</friendlyName>"
"<manufacturer>Belkin International Inc.</manufacturer>"
"<modelName>Socket</modelName>"
"<modelNumber>3.1415</modelNumber>"
"<UDN>uuid:";
static const char *g_wemo_setup_3 =
"</UDN>"
"<serialNumber>";
static const char *g_wemo_setup_4 =
"</serialNumber>"
"<presentationURL>http://";
static const char *g_wemo_setup_5 =
":80/</presentationURL>"
"<binaryState>0</binaryState>";
static const char *g_wemo_setup_6 =
	"<serviceList>"
		"<service>"
			"<serviceType>urn:Belkin:service:basicevent:1</serviceType>"
			"<serviceId>urn:Belkin:serviceId:basicevent1</serviceId>"
			"<controlURL>/upnp/control/basicevent1</controlURL>"
			"<eventSubURL>/upnp/event/basicevent1</eventSubURL>"
			"<SCPDURL>/eventservice.xml</SCPDURL>"
		"</service>"
		"<service>"
			"<serviceType>urn:Belkin:service:metainfo:1</serviceType>"
			"<serviceId>urn:Belkin:serviceId:metainfo1</serviceId>"
			"<controlURL>/upnp/control/metainfo1</controlURL>"
			"<eventSubURL>/upnp/event/metainfo1</eventSubURL>"
			"<SCPDURL>/metainfoservice.xml</SCPDURL>"
		"</service>"
	"</serviceList>"
	"</device>"
"</root>\r\n";

const char *g_wemo_msearch =
"HTTP/1.1 200 OK\r\n"
"CACHE-CONTROL: max-age=86400\r\n"
"DATE: Fri, 15 Apr 2016 04:56:29 GMT\r\n"
"EXT:\r\n"
"LOCATION: http://%s:80/setup.xml\r\n"
"OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n"
"01-NLS: b9200ebb-736d-4b93-bf03-835149d13983\r\n"
"SERVER: Unspecified, UPnP/1.0, Unspecified\r\n"
"ST: %s\r\n"                // type1 = urn:Belkin:device:**, type2 = upnp:rootdevice
"USN: uuid:%s::%s\r\n"      // type1 = urn:Belkin:device:**, type2 = upnp:rootdevice
"X-User-Agent: redsonic\r\n"
"\r\n";


const char *g_wemo_metaService =
"<scpd xmlns=\"urn:Belkin:service-1-0\">"
"<specVersion>"
"<major>1</major>"
"<minor>0</minor>"
"</specVersion>"
"<actionList>"
"<action>"
"<name>GetMetaInfo</name>"
"<argumentList>"
"<retval />"
"<name>GetMetaInfo</name>"
"<relatedStateVariable>MetaInfo</relatedStateVariable>"
"<direction>in</direction>"
"</argumentList>"
"</action>"
"</actionList>"
"<serviceStateTable>"
"<stateVariable sendEvents=\"yes\">"
"<name>MetaInfo</name>"
"<dataType>string</dataType>"
"<defaultValue>0</defaultValue>"
"</stateVariable>"
"</serviceStateTable>"
"</scpd>\r\n\r\n";

const char *g_wemo_eventService =
"<scpd xmlns=\"urn:Belkin:service-1-0\">"
"<actionList>"
"<action>"
"<name>SetBinaryState</name>"
"<argumentList>"
"<argument>"
"<retval/>"
"<name>BinaryState</name>"
"<relatedStateVariable>BinaryState</relatedStateVariable>"
"<direction>in</direction>"
"</argument>"
"</argumentList>"
"</action>"
"<action>"
"<name>GetBinaryState</name>"
"<argumentList>"
"<argument>"
"<retval/>"
"<name>BinaryState</name>"
"<relatedStateVariable>BinaryState</relatedStateVariable>"
"<direction>out</direction>"
"</argument>"
"</argumentList>"
"</action>"
"</actionList>"
"<serviceStateTable>"
"<stateVariable sendEvents=\"yes\">"
"<name>BinaryState</name>"
"<dataType>bool</dataType>"
"<defaultValue>0</defaultValue>"
"</stateVariable>"
"<stateVariable sendEvents=\"yes\">"
"<name>level</name>"
"<dataType>string</dataType>"
"<defaultValue>0</defaultValue>"
"</stateVariable>"
"</serviceStateTable>"
"</scpd>\r\n\r\n";

static char *g_serial = 0;
static char *g_uid = 0;
static int stat_searchesReceived = 0;
static int stat_setupXMLVisits = 0;
static int stat_metaServiceXMLVisits = 0;
static int stat_eventsReceived = 0;
static int stat_eventServiceXMLVisits = 0;

// Function to retrieve the state of all relays in a formatted string
static int GetAllRelayStates(char *outBuffer, int maxLen) {
    int offset = 0;
    for (int i = 0; i < CHANNEL_MAX; i++) {
        if (h_isChannelRelay(i)) {
            offset += snprintf(outBuffer + offset, maxLen - offset, "<Relay%d>%d</Relay%d>", i + 1, CHANNEL_Get(i), i + 1);
            if (offset >= maxLen) break;
        }
    }
    return offset;
}

// Function to parse relay-specific commands
static void HandleRelayCommand(const char *cmd) {
    int relayIndex;
    char state[4];
    if (sscanf(cmd, "POWER%d %3s", &relayIndex, state) == 2) {
        relayIndex--; // Convert 1-based index to 0-based
        if (relayIndex >= 0 && relayIndex < CHANNEL_MAX && h_isChannelRelay(relayIndex)) {
            if (strcasecmp(state, "ON") == 0) {
                CHANNEL_Set(relayIndex, 1, 0); // Turn relay ON
            } else if (strcasecmp(state, "OFF") == 0) {
                CHANNEL_Set(relayIndex, 0, 0); // Turn relay OFF
            }
        }
    }
}

static int WEMO_BasicEvent1(http_request_t* request) {
    const char* cmd = request->bodystart;
    char relayStates[512];

    addLogAdv(LOG_INFO, LOG_FEATURE_HTTP, "Wemo post event %s", cmd);

    // Parse commands for specific relays
    if (strstr(cmd, "SetBinaryState")) {
        HandleRelayCommand(cmd);
    }

    // Collect all relay states
    GetAllRelayStates(relayStates, sizeof(relayStates));

    // Generate the response
    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_response_1);
    poststr(request, "<RelayStates>");
    poststr(request, relayStates);
    poststr(request, "</RelayStates>");
    poststr(request, "</s:Body>");
    poststr(request, "</s:Envelope>\r\n");

    stat_eventsReceived++;
    return 0;
}

static int WEMO_Setup(http_request_t* request) {
    char relayStates[512];
    GetAllRelayStates(relayStates, sizeof(relayStates));

    http_setup(request, httpMimeTypeXML);
    poststr(request, g_wemo_setup_1);
    poststr(request, CFG_GetDeviceName());
    poststr(request, g_wemo_setup_2);
    poststr(request, g_uid);
    poststr(request, g_wemo_setup_3);
    poststr(request, HAL_GetMyIPString());
    poststr(request, g_wemo_setup_4);
    poststr(request, g_serial);
    poststr(request, g_wemo_setup_5);
    poststr(request, "<RelayStates>");
    poststr(request, relayStates);
    poststr(request, "</RelayStates>");
    poststr(request, g_wemo_setup_6);
    poststr(request, NULL);

    stat_setupXMLVisits++;
    return 0;
}

void WEMO_Init() {
    char uid[64];
    char serial[32];
    unsigned char mac[8];

    WiFI_GetMacAddress((char*)mac);
    snprintf(serial, sizeof(serial), "201612%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    snprintf(uid, sizeof(uid), "Socket-1_0-%s", serial);

    g_serial = strdup(serial);
    g_uid = strdup(uid);

    HTTP_RegisterCallback("/upnp/control/basicevent1", HTTP_POST, WEMO_BasicEvent1, 0);
    HTTP_RegisterCallback("/eventservice.xml", HTTP_GET, WEMO_EventService, 0);
    HTTP_RegisterCallback("/metainfoservice.xml", HTTP_GET, WEMO_MetaInfoService, 0);
    HTTP_RegisterCallback("/setup.xml", HTTP_GET, WEMO_Setup, 0);
}
