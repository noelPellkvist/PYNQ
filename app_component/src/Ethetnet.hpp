#pragma once
#include "netif/xadapter.h"
#include "lwip/tcp.h"
#include <vector>
#include "EthernetDefs.h"

class Ethenet
{
    public:
        Ethenet(u32 ip1, u32 ip2, u32 ip3, u32 ip4, bool isServer = true);
        ~Ethenet() = default;

        void Poll();
        void Send(void* data, size_t length);

        uint8_t GetCurrentPackageType() const { return header.package_type; }
        

    private:
        struct netif netif;
        struct tcp_pcb *pcb;
        struct tcp_pcb* m_pcbConnection = nullptr;
        std::vector<char> currentData;
        bool m_bufferDone = false;
        Ethernet_header_t header;

        static err_t AcceptCallback(void *arg, struct tcp_pcb *newpcb, err_t err);
        static err_t ReceiveCallback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
        static void ErrorCallback(void *arg, err_t err);
        static err_t ConnectCallback(void* arg, struct tcp_pcb* tpcb, err_t err);
        void Connect(u32 ip1, u32 ip2, u32 ip3, u32 ip4);
        

    public:
        bool m_IsServer;
        bool HasFinishedData() { return m_bufferDone; }
        const char* GetPayload() { return currentData.data(); }
        size_t GetPayloadSize() { return currentData.size(); }
        void DoneWithPayload();
};
