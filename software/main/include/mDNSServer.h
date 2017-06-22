#pragma once
#include "mdns.h"


class mDNSServer;

class mDNSServer
{
    public:
        static mDNSServer *get_instance();
    private:
        static void mdns_monitor(void *PvParameters);
        static mDNSServer *instance;
        mDNSServer();
};
