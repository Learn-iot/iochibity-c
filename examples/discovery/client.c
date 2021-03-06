//******************************************************************
//
// Copyright 2016 NORC at the University of Chicago
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>

#include "ocstack.h"
#include "logger.h"
#include "ocpayload.h"
#include "payload_logging.h"

#include "client.h"
#include "button.h"
#include "led.h"

#define TAG "minclient"

#define DEFAULT_CONTEXT_VALUE 0x99

static int UnicastDiscovery = 0;

pthread_t ui_thread;

/* %s will be replaced by IP address */
static const char *RSC_URI_RESOURCES = "%s/oic/res";
static const char *RSC_URI_PLATFORM  = "%s/oic/p";
static const char *PLATFORM_DISCOVERY_QUERY = "%s/oic/p";

static const char *RSC_URI_DEVICE    = "%s/oic/d";

static char discoveryAddr[100];

/* Platform Descriptor: OCPlatformInfo
 * This structure describes the platform properties. All non-Null properties will be
 * included in a platform discovery request. */
OCPlatformInfo platform_info =
  {
    .platformID			= "clientPlatformID",
    .manufacturerName		= "clientName",
    .manufacturerUrl		= "clientManufacturerUrl",
    .modelNumber		= "clientModelNumber",
    .dateOfManufacture		= "clientDateOfManufacture",
    .platformVersion		= "clientPlatformVersion",
    .operatingSystemVersion	= "clientOS",
    .hardwareVersion		= "clientHardwareVersion",
    .firmwareVersion		= "clientFirmwareVersion",
    .supportUrl			= "clientSupportUrl",
    .systemTime			= "2015-05-15T11.04"
  };

/* Device Descriptor: OCDeviceInfo
 * This structure is expected as input for device properties.
 * device name is mandatory and expected from the application.
 * device id of type UUID will be generated by the stack. */
OCDeviceInfo device_info =
  {
    .deviceName = "clientDeviceName",
    /* OCStringLL *types; */
    .specVersion = "clientDeviceSpecVersion" /* device specification version */
    //.dataModelVersions = "clientDeviceModleVersion"
  };

/* const char *deviceUUID = "myDeviceUUID"; */
/* const char *version = "myVersion"; */

OCEntityHandlerResult
default_request_dispatcher (OCEntityHandlerFlag flag,
			    OCEntityHandlerRequest *oic_request, /* just like HttpRequest */
			    char *uri,
			    void *cb /*callbackParam*/)
{
    OCEntityHandlerResult ehResult = OC_EH_OK;
    OCEntityHandlerResponse response;
    return ehResult;
}

#define SAMPLE_MAX_NUM_OBSERVATIONS     8
#define SAMPLE_MAX_NUM_POST_INSTANCE  2

observers_t interestedObservers[SAMPLE_MAX_NUM_OBSERVATIONS];

pthread_t threadId_observe;
/* pthread_t threadId_presence; */

static bool observeThreadStarted = false;

/* #ifdef WITH_PRESENCE */
/* #define numPresenceResources (2) */
/* #endif */

int gQuitFlag = 0;
int waiting   = 0;

struct ResourceNode *resourceList;

/* SIGINT handler: set gQuitFlag to 1 for graceful termination */
void handleSigInt(int signum)
{
    if (signum == SIGINT)
    {
        gQuitFlag = 1;
    }
}

static void PrintUsage()
{
    printf("Usage : ocserver -o <0|1>\n");
    printf("-o 0 : Notify all observers\n");
    printf("-o 1 : Notify list of observers\n");
}

/* **************************************************************** */
void queryResource()
{
    /* switch(TestCase) */
    /* { */
    /*     case TEST_DISCOVER_REQ: */
    /*         break; */
    /*     case TEST_NON_CON_OP: */
    /*         InitGetRequest(OC_LOW_QOS); */
    /*         InitPutRequest(OC_LOW_QOS); */
    /*         InitPostRequest(OC_LOW_QOS); */
    /*         break; */
    /*     case TEST_CON_OP: */
    /*         InitGetRequest(OC_HIGH_QOS); */
    /*         InitPutRequest(OC_HIGH_QOS); */
    /*         InitPostRequest(OC_HIGH_QOS); */
    /*         break; */
    /*     default: */
    /*         PrintUsage(); */
    /*         break; */
    /* } */
}

void printResourceList()
{
    struct ResourceNode * iter;
    iter = resourceList;
    OIC_LOG(INFO, TAG, "Resource List: ");
    while(iter)
    {
        OIC_LOG(INFO, TAG, "*****************************************************");
        OIC_LOG_V(INFO, TAG, "sid = %s",iter->sid);
        OIC_LOG_V(INFO, TAG, "uri = %s", iter->uri);
        OIC_LOG_V(INFO, TAG, "ip = %s", iter->endpoint.addr);
        OIC_LOG_V(INFO, TAG, "port = %d", iter->endpoint.port);
        switch (iter->endpoint.adapter)
        {
            case OC_ADAPTER_IP:
                OIC_LOG(INFO, TAG, "connType = Default (IPv4)");
                break;
            case OC_ADAPTER_GATT_BTLE:
                OIC_LOG(INFO, TAG, "connType = BLE");
                break;
            case OC_ADAPTER_RFCOMM_BTEDR:
                OIC_LOG(INFO, TAG, "connType = BT");
                break;
            default:
                OIC_LOG(INFO, TAG, "connType = Invalid connType");
                break;
        }
        OIC_LOG(INFO, TAG, "*****************************************************");
        iter = iter->next;
    }
}

/* This function searches for the resource(sid:uri) in the ResourceList.
 * If the Resource is found in the list then it returns 0 else insert
 * the resource to front of the list and returns 1.
 */
int insertResource(const char * sid, char const * uri,
            const OCClientResponse * clientResponse)
{
    struct ResourceNode * iter = resourceList;
    char * sid_cpy =  OICStrdup(sid);
    char * uri_cpy = OICStrdup(uri);

    //Checking if the resource(sid:uri) is new
    while(iter)
    {
        if((strcmp(iter->sid, sid) == 0) && (strcmp(iter->uri, uri) == 0))
        {
            OICFree(sid_cpy);
            OICFree(uri_cpy);
            return 0;
        }
        else
        {
            iter = iter->next;
        }
    }

    //Creating new ResourceNode
    if((iter = (struct ResourceNode *) OICMalloc(sizeof(struct ResourceNode))))
    {
        iter->sid = sid_cpy;
        iter->uri = uri_cpy;
        iter->endpoint = clientResponse->devAddr;
        iter->next = NULL;
    }
    else
    {
        OIC_LOG(ERROR, TAG, "Memory not allocated to ResourceNode");
        OICFree(sid_cpy);
        OICFree(uri_cpy);
        return -1;
    }

    //Adding new ResourceNode to front of the ResourceList
    if(!resourceList)
    {
        resourceList = iter;
    }
    else
    {
        iter->next = resourceList;
        resourceList = iter;
    }
    return 1;
}

void collectUniqueResource(const OCClientResponse * clientResponse)
{
    OCDiscoveryPayload* pay = (OCDiscoveryPayload*) clientResponse->payload;
    OCResourcePayload* res = pay->resources;

    // Including the NUL terminator, length of UUID string of the form:
    //   "a62389f7-afde-00b6-cd3e-12b97d2fcf09"
#   define UUID_LENGTH 37

    char sidStr[UUID_LENGTH];

    while(res) {

        int ret = snprintf(sidStr, UUID_LENGTH,
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                pay->sid[0], pay->sid[1], pay->sid[2], pay->sid[3],
                pay->sid[4], pay->sid[5], pay->sid[6], pay->sid[7],
                pay->sid[8], pay->sid[9], pay->sid[10], pay->sid[11],
                pay->sid[12], pay->sid[13], pay->sid[14], pay->sid[15]
                );

        if (ret == UUID_LENGTH - 1)
        {
            if(insertResource(sidStr, res->uri, clientResponse) == 1)
            {
                OIC_LOG_V(INFO,TAG,"%s%s%s%s\n",sidStr, ":", res->uri, " is new");
                printResourceList();
                queryResource();
            }
            else {
                OIC_LOG_V(INFO,TAG,"%s%s%s%s\n",sidStr, ":", res->uri, " is old");
            }
        }
        else
        {
            OIC_LOG(ERROR, TAG, "Could Not Retrieve the Server ID");
        }

        res = res->next;
    }
}

/* ****************************************************************
 *     Service Routines
 ****************************************************************/
OCStackApplicationResult svc_platform_discovery_response(void* ctx,
							 OCDoHandle handle,
							 OCClientResponse * clientResponse)
{
    if (ctx == (void*) DEFAULT_CONTEXT_VALUE)
    {
        OIC_LOG(INFO, TAG, "Callback Context for Platform DISCOVERY  recd");
    }

    if (clientResponse)
    {
        OIC_LOG(INFO, TAG, ("Discovery Response:"));
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
	/* OCPayloadLogPlatform(INFO, (OCPlatformPayload *)clientResponse->payload); */
	/* printf("dev addr: %x\n", clientResponse->devAddr); */
	/* printf("identity: %x\n"); */
	/* printf("sequence nbr: %x\n"); */
	/* printf("resource uri: %s\n", clientResponse->resourceUri); */
	/* printf("vnd hdr options count: %d\n", clientResponse->numRcvdVendorSpecificHeaderOptions); */
    }
    else
    {
        OIC_LOG_V(INFO, TAG, "PlatformDiscoveryReqCB received Null clientResponse");
    }

    waiting = 0;		/* tell ui thread we're done */
    return (UnicastDiscovery) ? OC_STACK_DELETE_TRANSACTION : OC_STACK_KEEP_TRANSACTION;
}

OCStackApplicationResult svc_device_discovery_response(void* ctx,
						       OCDoHandle handle,
						       OCClientResponse * clientResponse)
{
    if (ctx == (void*) DEFAULT_CONTEXT_VALUE)
    {
        OIC_LOG(INFO, TAG, "Callback Context for Device DISCOVER query recvd successfully");
        OIC_LOG_V(INFO, TAG, "Response dev: %s", clientResponse->devAddr.addr);
        OIC_LOG_V(INFO, TAG, "Response port: %d", clientResponse->devAddr.port);
        OIC_LOG_V(INFO, TAG, "Response uri: %s", clientResponse->resourceUri);
    }

    if (clientResponse)
    {
        OIC_LOG(INFO, TAG, ("Discovery Response:"));
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);
    }
    else
    {
        OIC_LOG_V(INFO, TAG, "PlatformDiscoveryReqCB received Null clientResponse");
    }

    waiting = 0;		/* tell ui thread we're done */
    return (UnicastDiscovery) ? OC_STACK_DELETE_TRANSACTION : OC_STACK_KEEP_TRANSACTION;
}

OCStackApplicationResult svc_resource_discovery_response(void *ctx,
							 OCDoHandle handle,
							 OCClientResponse *clientResponse)
{
    if (ctx == (void*)DEFAULT_CONTEXT_VALUE)
    {
        OIC_LOG(INFO, TAG, "\n<====Callback Context for RESOURCE DISCOVERY action "
               "received successfully====>");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "\n<====Callback Context for DISCOVERY fail====>");
    }

    if (clientResponse)
    {
        OIC_LOG_V(INFO, TAG,
                "Client Device =====> Discovered @ %s:%d",
                clientResponse->devAddr.addr,
                clientResponse->devAddr.port);
        OIC_LOG_PAYLOAD(INFO, clientResponse->payload);

        /* collectUniqueResource(clientResponse); */
    }
    else
    {
        OIC_LOG(ERROR, TAG, "<====DISCOVERY Callback fail to receive clientResponse====>\n");
    }

    waiting = 0;		/* tell ui thread we're done */
    return (UnicastDiscovery) ?
           OC_STACK_DELETE_TRANSACTION : OC_STACK_KEEP_TRANSACTION ;
}

/* ****************************************************************
 *        ACTIONS
 **************************************************************** */

int discover_platform(OCQualityOfService qos)
{
    OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);

    OCStackResult ret;
    OCCallbackData cbData;
    char szQueryUri[64] = { 0 };

    snprintf(szQueryUri, sizeof (szQueryUri) - 1, PLATFORM_DISCOVERY_QUERY, discoveryAddr);

    cbData.cb = svc_device_discovery_response;
    cbData.context = (void*)DEFAULT_CONTEXT_VALUE;
    cbData.cd = NULL;

    ret = OCDoResource(NULL, OC_REST_DISCOVER, szQueryUri, NULL, 0, CT_DEFAULT,
                       (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS,
                       &cbData, NULL, 0);
    if (ret != OC_STACK_OK)
    {
        OIC_LOG(ERROR, TAG, "OCStack device error");
    }

    return ret;
}

int discover_device(OCQualityOfService qos)
{
    OIC_LOG_V(INFO, TAG, "\n\nExecuting %s", __func__);

    OCStackResult ret;
    OCCallbackData cbData;
    char szQueryUri[100] = { 0 };

    snprintf(szQueryUri, sizeof (szQueryUri) - 1, RSC_URI_DEVICE, discoveryAddr);

    cbData.cb = svc_device_discovery_response;
    cbData.context = (void*)DEFAULT_CONTEXT_VALUE;
    cbData.cd = NULL;

    ret = OCDoResource(NULL, OC_REST_DISCOVER, szQueryUri, NULL, 0, CT_DEFAULT,
                       (qos == OC_HIGH_QOS) ? OC_HIGH_QOS : OC_LOW_QOS,
                       &cbData, NULL, 0);
    if (ret != OC_STACK_OK)
    {
        OIC_LOG(ERROR, TAG, "OCStack device error");
    }

    return ret;
}

int discover_resources(OCQualityOfService qos)
{
    OCStackResult ret;
    OCCallbackData cbData;
    char queryUri[200];
    char ipaddr[100] = { '\0' };

    /* if (UnicastDiscovery) */
    /* { */
    /*     OIC_LOG(INFO, TAG, "Enter IP address (with optional port) of the Server hosting resource\n"); */
    /*     OIC_LOG(INFO, TAG, "IPv4: 192.168.0.15:45454\n"); */
    /*     OIC_LOG(INFO, TAG, "IPv6: [fe80::20c:29ff:fe1b:9c5]:45454\n"); */

    /*     if (fgets(ipaddr, sizeof (ipaddr), stdin)) */
    /*     { */
    /*         StripNewLineChar(ipaddr); //Strip newline char from ipaddr */
    /*     } */
    /*     else */
    /*     { */
    /*         OIC_LOG(ERROR, TAG, "!! Bad input for IP address. !!"); */
    /*         return OC_STACK_INVALID_PARAM; */
    /*     } */
    /* } */

    snprintf(queryUri, sizeof (queryUri), RSC_URI_RESOURCES, ipaddr);

    printf("Query uri: %s\n", queryUri);

    cbData.cb = svc_resource_discovery_response;
    cbData.context = (void*)DEFAULT_CONTEXT_VALUE;
    cbData.cd = NULL;

    ret = OCDoResource(NULL,
		       OC_REST_DISCOVER,
		       queryUri,
		       0, 0, CT_DEFAULT, OC_LOW_QOS,
		       &cbData,
		       NULL, 0);
    if (ret != OC_STACK_OK)
    {
        OIC_LOG(ERROR, TAG, "OCStack resource error");
    }
    return ret;
}

void *prompt_user(void * arg)
{
  char action[1];
  struct timespec ts = { .tv_sec = 0,
			 .tv_nsec = 100000000L };

  while(!gQuitFlag) {
    if (waiting)
      nanosleep(&ts, NULL);
    else {
      printf("\nChoose an action: 1) Platform discovery  2) Device discovery  3) Resource discovery q) Quit\n");
      scanf("%s", (char*)&action);
      switch(*action) {
      case '1':
	discover_platform(OC_LOW_QOS);
	waiting = 1;
	break;
      case '2':
	discover_device(OC_LOW_QOS);
	waiting = 1;
	break;
      case '3':
	discover_resources(OC_LOW_QOS);
	waiting = 1;
	break;
      case 'q':
	gQuitFlag = 1;
	fflush(stdout);
	break;
      default:
	printf("Unrecognized action: %s\n", action);
      }
    }
  }
  return NULL;
}

/****************************************************************
 *  MAIN
 *  1. initialize rmgr (stack)
 *  2. register platform info
 *  3. register device info
 *  4. register service routine for default rsvp
 *  5. enter processing loop
 ****************************************************************/
int main(int argc, char* argv[])
{
    int opt = 0;

    /* while ((opt = getopt(argc, argv, "o:s:p:d:u:w:r:j:")) != -1) { */
    /*   switch(opt) { */
    /*   case 'o': */
    /* 	g_observe_notify_type = atoi(optarg); */
    /* 	break; */
    /*   default: */
    /* 	PrintUsage(); */
    /* 	return -1; */
    /*   } */
    /* } */

    /* if ((g_observe_notify_type != 0) && (g_observe_notify_type != 1)) { */
    /*     PrintUsage(); */
    /*     return -1; */
    /* } */

    printf("Client is starting...");

    OCStackResult op_result;

    /* 1. initialize */
    /* use default transport flags, so stack will pick a transport */
    op_result = OCInit1(OC_CLIENT, OC_DEFAULT_FLAGS, OC_DEFAULT_FLAGS);
    if (op_result != OC_STACK_OK) {
        printf("OCStack init error\n");
        return 0;
    }

    /* 2. register platform info */
    /* WARNING: platform registration only allowed for servers */
    /* To enable the following, use OC_SERVER or OC_CLIENT_SERVER in the call to OCInit1 above */
    /* op_result = OCSetPlatformInfo(platform_info); */
    /* if (op_result != OC_STACK_OK) { */
    /*     printf("Platform Registration failed!\n"); */
    /*     exit (EXIT_FAILURE); */
    /* } */

    /* 3. register default device info */
    /* WARNING: device registration only allowed for servers */
    /* OCResourcePayloadAddStringLL(&device_info.types, "oic.d.tv"); */
    /* op_result = OCSetDeviceInfo(device_info); */
    /* if (op_result != OC_STACK_OK) { */
    /*     printf("Device Registration failed!\n"); */
    /*     exit (EXIT_FAILURE); */
    /* } */

    // Initialize observations data structure for the resource
    /* for (uint8_t i = 0; i < SAMPLE_MAX_NUM_OBSERVATIONS; i++) */
    /* { */
    /*     interestedObservers[i].valid = false; */
    /* } */


    /*
     * Create a thread for generating changes that cause presence notifications
     * to be sent to clients
     */

    /* #ifdef WITH_PRESENCE */
    /* pthread_create(&threadId_presence, NULL, presenceNotificationGenerator, (void *)NULL); */
    /* #endif */

    // Break from loop with Ctrl-C
    printf("Entering client main loop...\n");

    /* DeletePlatformInfo(); */
    /* DeleteDeviceInfo(); */

    signal(SIGINT, handleSigInt);

    pthread_create (&ui_thread, NULL, prompt_user, NULL);

    while (!gQuitFlag)
    {
        if (OCProcess() != OC_STACK_OK)
        {
            printf("OCStack process error\n");
            return 0;
        }
    }

    pthread_cancel(ui_thread);
    pthread_join(ui_thread, NULL);

    /* if (observeThreadStarted) */
    /* { */
    /*     pthread_cancel(threadId_observe); */
    /*     pthread_join(threadId_observe, NULL); */
    /* } */

    /* pthread_cancel(threadId_presence); */
    /* pthread_join(threadId_presence, NULL); */

    printf("Exiting client main loop...\n");

    if (OCStop() != OC_STACK_OK)
    {
        printf("OCStack stop error\n");
    }

    return 0;
}

