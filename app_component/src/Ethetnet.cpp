#include "Ethetnet.hpp"
#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "xparameters.h"
#include "xil_printf.h"
#include "lwip/timeouts.h"

#include "xparameters.h"
#include "xiltimer.h"
#include "sleep.h"

#define ECHO_SERVER_PORT 7

extern "C" u32_t sys_now(void)
{
    XTime t;
    XTime_GetTime(&t);

    return (u32_t)(t / (COUNTS_PER_SECOND / 1000));
}


static err_t TcpSend(struct tcp_pcb *tpcb, const char *data, u16_t len)
{
    err_t err = tcp_write(tpcb, data, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        xil_printf("tcp_write error: %d\r\n", err);
        return err;
    }

    err = tcp_output(tpcb);
    if (err != ERR_OK) {
        xil_printf("tcp_output error: %d\r\n", err);
    }
    return err;
}

Ethenet::Ethenet(u32 ip1, u32 ip2, u32 ip3, u32 ip4, bool isServer) : m_IsServer(isServer)
{
    ip_addr_t ipaddr, netmask, gw;

    IP4_ADDR(&ipaddr, ip1, ip2, ip3, ip4);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, ip1, ip2, ip3, 1);

    lwip_init();

    header.packageLength = 0;

    unsigned char mac_addr[6] = { 0x00, 0x0A, 0x35, 0x01, 0x02, 0x03 };

    if (!xemac_add(&netif, &ipaddr, &netmask, &gw,
                   NULL, XPAR_XEMACPS_0_BASEADDR)) {
        xil_printf("Error adding N/W interface\r\n");
        return;
    }

    netif_set_default(&netif);
    netif_set_up(&netif);
    netif_set_link_up(&netif);

    xil_printf("Ethernet initialized: %s\r\n", ip4addr_ntoa(&ipaddr));

    pcb = tcp_new();
    if (!pcb) {
        xil_printf("Error creating TCP PCB\r\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, ECHO_SERVER_PORT);
    if (err != ERR_OK) {
        xil_printf("TCP bind failed: %d\r\n", err);
        tcp_close(pcb);
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_arg(pcb, this);
    tcp_accept(pcb, AcceptCallback);

    xil_printf("TCP Echo Server started on port %d\r\n", ECHO_SERVER_PORT);

    if(!isServer) Connect(192, 168, 1, 10);
}

void Ethenet::Connect(u32 ip1, u32 ip2, u32 ip3, u32 ip4)
{
    ip_addr_t peer;
    IP4_ADDR(&peer, ip1, ip2, ip3, ip4);

    struct tcp_pcb* clientpcb = tcp_new();
    if (!clientpcb) {
        xil_printf("ERROR: Could not create client PCB\r\n");
        return;
    }

    tcp_arg(clientpcb, this);

    err_t err = tcp_connect(clientpcb, &peer, ECHO_SERVER_PORT, ConnectCallback);
    if (err != ERR_OK) {
        xil_printf("tcp_connect failed: %d\r\n", err);
    } else {
        xil_printf("Connecting to peer %s...\r\n", ip4addr_ntoa(&peer));
    }
}

err_t Ethenet::ConnectCallback(void* arg, struct tcp_pcb* tpcb, err_t err)
{
    xil_printf("Connected to peer!\r\n");
    Ethenet* self = static_cast<Ethenet*>(arg);
    tcp_recv(tpcb, ReceiveCallback);
    tcp_err(tpcb, ErrorCallback);

    if (!self->m_IsServer) {
    self->m_pcbConnection = tpcb;
    xil_printf("Client: storing PCB\n");
}

    return ERR_OK;
}


void Ethenet::Poll()
{
    xemacif_input(&netif);
    sys_check_timeouts();
}

err_t Ethenet::AcceptCallback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    xil_printf("New connection accepted\r\n");
    Ethenet* self = static_cast<Ethenet*>(arg);

    tcp_arg(newpcb, arg);

    tcp_recv(newpcb, ReceiveCallback);
    tcp_err(newpcb, ErrorCallback);
    if (self->m_IsServer) {
        self->m_pcbConnection = newpcb;
        xil_printf("Server: storing PCB\n");
    }
    return ERR_OK;
}

void Ethenet::Send(void* data, size_t length)
{
    xil_printf("Trying to send now!\r\n");
    if (m_pcbConnection != nullptr) {
    err_t err = TcpSend(m_pcbConnection, (const char*)data, length);
    xil_printf("Send result: %d\r\n", err);
} else {
    xil_printf("ERROR: No connection PCB set!\r\n");
}
}

err_t Ethenet::ReceiveCallback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    Ethenet* self = static_cast<Ethenet*>(arg);
    if (!p) {
        xil_printf("Connection closed\r\n");
        tcp_close(tpcb);
        return ERR_OK;
    }

    struct pbuf* q = p;
    while (q != nullptr) {
        const char* data = static_cast<const char*>(q->payload);
        self->currentData.insert(self->currentData.end(), data, data + q->len);
        q = q->next;
    }

    //xil_printf("Received %d bytes (total buffer size now %u)\r\n",
    //           p->tot_len, (unsigned)self->currentData.size());
    
    if(self->header.packageLength == 0)
    {
        self->header = *reinterpret_cast<Ethernet_header_t*>(self->currentData.data()); 
        xil_printf("Got header of type %d and length of %d\r\n", (unsigned int)self->header.package_type, (unsigned int)self->header.packageLength);
    }
    if (self->currentData.size() == self->header.packageLength && self->header.package_type != ACK && self->header.package_type != RECORDED_WAVEFORM) {
            self->m_bufferDone = true;
            Ethernet_header_t ackHeader;
            ackHeader.package_type = ACK;
            ackHeader.packageLength = sizeof(ackHeader);
            TcpSend(tpcb, (const char*)&ackHeader, sizeof(ackHeader));
            xil_printf("Sending ACK\r\n");
            
    } else if (self->currentData.size() == self->header.packageLength && self->header.package_type == ACK) {
        self->currentData.clear();
        self->m_bufferDone = false;
    }
    else if (self->currentData.size() == self->header.packageLength && self->header.package_type == RECORDED_WAVEFORM) {
        self->m_bufferDone = true;
    }
    
    tcp_recved(tpcb, p->len);
    pbuf_free(p);
    return ERR_OK;
}

void Ethenet::DoneWithPayload()
{
    m_bufferDone = false;
    header.packageLength = 0;
    currentData.clear();
}

void Ethenet::ErrorCallback(void *arg, err_t err)
{
    xil_printf("TCP error: %d\r\n", err);
}