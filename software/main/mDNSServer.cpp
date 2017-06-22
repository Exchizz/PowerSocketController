#include "mDNSServer.h"
#include "esp_log.h"
#include "WifiHandler.h"
#include <cstring>
static const char *TAG = "mDNS";

mDNSServer *mDNSServer::instance = nullptr;

mDNSServer *mDNSServer::get_instance()
{
    if(mDNSServer::instance == nullptr)
    {
        mDNSServer::instance = new mDNSServer();
    }
    return mDNSServer::instance;
}

mDNSServer::mDNSServer()
{
    xTaskCreatePinnedToCore(mDNSServer::mdns_monitor, "mDNSmonitor", 2048, this, 10, NULL, 0);

};

static void srv_http(struct mdns_service *service, void *txt_userdata)
{
   err_t res = mdns_resp_add_service_txtitem(service, "ehm..", 5);
   LWIP_ERROR("mdns add service txt failed\n", (res == ERR_OK), return);
   LWIP_UNUSED_ARG(txt_userdata);
}

void mDNSServer::mdns_monitor(__attribute__((unused)) void *PvParameters)
{
    // Housekeeping variables
    bool is_connected_sta = false;
    bool is_connected_ap = false;
    bool mdns_configured = false;
    esp_err_t err = ERR_OK;

    while(true){
      if(!is_connected_ap && (xEventGroupGetBits( WifiHandler::wifi_event_group ) & WifiHandler::AP_CONNECTED_BIT)){
        is_connected_ap = true;

        if(!mdns_configured ){
          mdns_configured = true;
          ESP_LOGI(TAG, "mdns_init run");
          mdns_resp_init();
        }

        struct netif *iface = esp_netif[TCPIP_ADAPTER_IF_AP];
        char  hostname[] = "powersocket";
        err = mdns_resp_add_netif(iface, hostname, 255);
        mdns_resp_add_service(iface,"powersocket","_http", DNSSD_PROTO_TCP, 80, 255, srv_http, NULL);
        if (err) {
          ESP_LOGE(TAG, "Failed starting mDNS AP: %d", err);
        } else {
          ESP_LOGI(TAG, "Sucessfully started mDNS AP !");
        }
      }

      if(!is_connected_sta && (xEventGroupGetBits( WifiHandler::wifi_event_group ) & WifiHandler::CONNECTED_BIT)){
        is_connected_sta = true;

        if(!mdns_configured ){
          mdns_configured = true;
          ESP_LOGI(TAG, "mdns_init run");
          mdns_resp_init();
        }

        struct netif *iface = esp_netif[TCPIP_ADAPTER_IF_STA];
        char  hostname[] = "powersocket";
        err = mdns_resp_add_netif(iface, hostname, 255);
        mdns_resp_add_service(iface,"powersocket","_http", DNSSD_PROTO_TCP, 80, 255, srv_http, NULL);
        if (err) {
          ESP_LOGE(TAG, "Failed starting mDNS STA: %d", err);
        } else {
          ESP_LOGI(TAG, "Sucessfully started mDNS STA !");
        }
      }

      vTaskDelay(10000 / portTICK_PERIOD_MS); //every 5 seconds
    }
}
