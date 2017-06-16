#include "mDNSServer.h"
#include "esp_log.h"
#include "WifiHandler.h"
#include <cstring>
static const char *TAG = "mdns";

static const char * arduTxtData[3] = {
    "board_rev=esp32_0.1",
    "sockets=3",
    "api=v1",
};

mdns_server_t *mDNSServer::server_sta = nullptr;
mdns_server_t *mDNSServer::server_ap = nullptr;

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


void advertise_mdns_records(mdns_server_t * iface){
      ESP_ERROR_CHECK( mdns_set_hostname(iface, "powersocket") );
      ESP_ERROR_CHECK( mdns_set_instance(iface, "A Powersocket") );

      ESP_ERROR_CHECK( mdns_service_add(iface, "_powersocket_http", "_tcp", 80) );
      ESP_ERROR_CHECK( mdns_service_txt_set(iface, "_powersocket_http", "_tcp", 3, arduTxtData) );
      ESP_ERROR_CHECK( mdns_service_add(iface, "_powersocket_https", "_tcp", 443) );
      ESP_ERROR_CHECK( mdns_service_txt_set(iface, "_powersocket_https", "_tcp", 3, arduTxtData) );
}


void mDNSServer::mdns_monitor(__attribute__((unused)) void *PvParameters)
{
    // Housekeeping variables
    bool is_connected_sta = false;
    bool is_connected_ap = false;
    esp_err_t err;

    while(true){
      if(!is_connected_sta && (xEventGroupGetBits( WifiHandler::wifi_event_group ) & WifiHandler::AP_CONNECTED_BIT)){
        is_connected_sta = true;

        err = mdns_init(TCPIP_ADAPTER_IF_AP, &mDNSServer::server_ap);
        if (err) {
          ESP_LOGE(TAG, "Failed starting mDNS AP: %d", err);
        } else {
          ESP_LOGI(TAG, "Sucessfully started mDNS AP !");
        }
        advertise_mdns_records(mDNSServer::server_ap);
      }

      if(!is_connected_ap && (xEventGroupGetBits( WifiHandler::wifi_event_group ) & WifiHandler::CONNECTED_BIT)){
        is_connected_ap = true;

        err = mdns_init(TCPIP_ADAPTER_IF_STA, &mDNSServer::server_sta);
        if (err) {
          ESP_LOGE(TAG, "Failed starting mDNS STA: %d", err);
        } else {
          ESP_LOGI(TAG, "Sucessfully started mDNS STA !");
        }
        advertise_mdns_records(mDNSServer::server_sta);
      }
      vTaskDelay(10000 / portTICK_PERIOD_MS); //every 5 seconds
    }
};
