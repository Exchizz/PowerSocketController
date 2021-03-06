#pragma once
#include <cstdint>
#include "mongoose.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "WifiHandler.h"
#include "SwitchHandler.h"
#include "TimeKeeper.h"
#include "CurrentMeasurer.h"
struct OTA_status
{
    esp_ota_handle_t out_handle;
    esp_partition_t operate_partition;
};

class HttpServer
{
    public:
        HttpServer(const char *_port, bool use_ssl = false);
        ~HttpServer();
        bool start();
        bool stop();
        void ev_handler(struct mg_connection *c, int ev, void *p);
        const char *port;
        static void http_thread_wrapper(void *PvParameters);
        static void ev_handler_wrapper(struct mg_connection *c, int ev, void *p);
        void http_thread();
        static void OTA_endpoint(struct mg_connection *c, int ev, void *p);
        static void reboot(struct mg_connection *c, int ev, void *p);
        static void reset(struct mg_connection *c, int ev, void *p);
        static void SWITCH_ENDPOINT(struct mg_connection *c, int ev, void *p);
        static void RECALIB_CURRENT_ENDPOINT(struct mg_connection *c, int ev, void *p);

        static void index(struct mg_connection *c, int ev, void *p);
        static void SETTING(struct mg_connection *c, int ev, void *p);
        static void handle_get_ip_info(struct mg_connection *c, struct http_message *hm, tcpip_adapter_if_t adapter);
        static void handle_get_uptime(struct mg_connection *c, struct http_message *hm);
        static void handle_get_switch_state(struct mg_connection *c, struct http_message *hm);
        static void handle_get_calibrations(struct mg_connection *c, struct http_message *hm);
        bool ota_init();
        OTA_status ota_status;
        struct mg_serve_http_opts s_http_server_opts;
        void handle_ssi(struct mg_connection *c, void *p);
        SettingsHandler *s_handler = nullptr;
        SwitchHandler *switch_handler = nullptr;
        CurrentMeasurer *cur_measurer = nullptr;
        TimeKeeper *t_keeper = nullptr;

    private:
        volatile bool running;
        bool use_ssl;
        int do_reboot = 0;

};
