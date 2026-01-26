#include "Uart.hpp"
#include "WifiHandler.hpp"
#include "MCP4261.h"
#include "MCP4261_helper.h"

#include "Ethetnet.hpp"
#include <ostream>
#include <stdint.h>
//#include <xspi.h>
#include "lwip/tcpbase.h"
//#include "sleep.h"
#include <cstdint>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include "xil_printf.h"
#include <malloc.h>
#include <iomanip>

#include <vector>
//#define DATALENGTH 8
#define DATALENGTH 65536

#define AMP_RATE 5
#define OFF_RATE 10

Uart uart;
EspEvents ev;
WifiHandler* handler;
Ethenet* ethernet;

XSpi spi_instance;
XSpi_Config *spi_config;
XGpio GPIOs;
MCP4261* mcp1;
MCP4261* mcp2;


std::vector<char> storedSignal;

void Scan();

void HandleSetParameters(const HttpRequest& req)
{
    if (req.dataType == DataType::JSON) {
            try
            {
                uint32_t sleep_time=500;
                auto& m = req.toMap();
                auto freq = m.at("frequency");
                auto amp = m.at("amplitude");
                auto off = m.at("offset");
                std::string body = R"({"ok":true})";
                std::string resp = buildHttpResponse(200, body, "application/json");
                uint16_t amplitude = static_cast<uint16_t>(AMP_RATE*std::stof(amp));
                uint16_t offset = static_cast<uint16_t>(OFF_RATE*std::stof(off));

                std::cout << "Setting amplitude to " << amplitude << " and offset to " << offset << " via spi now" << std::endl;
                TEST_set(mcp1, amplitude, amplitude,VOLATILE, sleep_time);
                TEST_set(mcp2, offset, offset,VOLATILE, sleep_time);
                //usleep(10*sleep_time);

                //TEST_get(mcp1, VOLATILE, sleep_time);
                req.sendFn(req.link_id, resp);
                Scan();
            }
            catch (...)
            {
                std::string body = R"({"ok":false})";
                std::string resp = buildHttpResponse(418, body, "application/json");
                req.sendFn(req.link_id, resp);
            }
        }
        else
        {
            std::string body = R"({"ok":false})";
            std::string resp = buildHttpResponse(418, body, "application/json");
            req.sendFn(req.link_id, resp);
        }
        
}

void HandleReadParameters(const HttpRequest& req)
{
    if (req.dataType == DataType::JSON) {
            try
            {
                uint32_t sleep_time {50000};
                uint16_t read_amplitute {0};
                uint16_t read_offset {0};
                if (!mcp1->get_wiper(0, read_amplitute, VOLATILE)) printf("FAIL READ 0!! \n");
                //else printf("wiper 0 read value is %d \n", read_amplitute);
                usleep(sleep_time);
                read_amplitute=read_amplitute/AMP_RATE;

                if (!mcp2->get_wiper(1, read_offset, VOLATILE)) printf("FAIL READ 1!! \n");
                //else printf("wiper 1 read value is %d \n", read_offset);
                usleep(sleep_time);
                read_offset=read_offset/OFF_RATE;
                
                std::string body = std::string("{\"frequency\":") + std::to_string(114514) +
                                ", \"amplitude\":" + std::to_string(read_amplitute) +
                                ", \"offset\":"    + std::to_string(read_offset) +
                                "}";
                printf("reading MCP chips from UI\n");
                TEST_get(mcp1, VOLATILE, sleep_time);
                TEST_get(mcp2, VOLATILE, sleep_time);

                std::string resp = buildHttpResponse(200, body, "application/json");
                req.sendFn(req.link_id, resp);
            }
            catch (...)
            {
                std::string body = R"({"ok":false})";
                std::string resp = buildHttpResponse(418, body, "application/json");
                req.sendFn(req.link_id, resp);
            }
        }
        else
        {
            std::string body = R"({"ok":false})";
            std::string resp = buildHttpResponse(418, body, "application/json");
            req.sendFn(req.link_id, resp);
        }
        
}

void GetAIModel(const HttpRequest& req)
{
    if (req.dataType == DataType::BINARY) {
            try
            {
                std::cout << "Got Binary payload of size " << req.binarySize() << std::endl;
                const uint16_t* data = reinterpret_cast<const uint16_t*>(req.binaryData());
                std::cout << "First entry of the data was 0x" << std::hex << data[0] << std::endl;
                std::cout << "Second entry of the data was 0x" << std::hex << data[1] << std::endl;
                std::cout << "Last entry of the data was 0x" << std::hex << data[31999] << std::endl;
                std::string body = R"({"ok":true})";
                std::string resp = buildHttpResponse(200, body, "application/json");
                
                req.sendFn(req.link_id, resp);
            }
            catch (...)
            {
                std::string body = R"({"ok":false})";
                std::string resp = buildHttpResponse(418, body, "application/json");
                req.sendFn(req.link_id, resp);
            }
        }
        else
        {
            std::string body = R"({"ok":false})";
            std::string resp = buildHttpResponse(418, body, "application/json");
            req.sendFn(req.link_id, resp);
        }

}

void StartWifiUart()
{
    
    ev.onConnect = [](int id) { std::cout << "[CONNECT TO WEBSITE] link " << id << "\n"; };
    ev.onClosed = [](int id) { std::cout << "[CLOSED FROM WEBSITE] link " << id << "\n"; };
    ev.onStaConnected = [](std::string mac) { std::cout << "[CONNECT TO HOTSPOT] " << mac << "\n"; };
    ev.onStaDisconnected = [](std::string mac) { std::cout << "[DISCONNECT FROM HOTSPOT] " << mac << "\n"; };
    ev.onDistStaIp = [](std::string mac, std::string ip) { std::cout << "[DEVICE GOT AN IP] " << mac << " -> " << ip << "\n"; };

    handler = new WifiHandler(ev, uart);
    handler->onHttp(HttpMethod::POST, "/setParameters", HandleSetParameters);
    handler->onHttp(HttpMethod::POST, "/readMCP", HandleReadParameters);
    handler->onHttp(HttpMethod::POST, "/rawSignals", GetAIModel);

    handler->onHttp(HttpMethod::POST, "/reset", [](const HttpRequest& req) {
        uint16_t viper;
        //mcp1->get_wiper(0, &wiper, true);
        std::cout << "Reset now with viper at: " << viper << std::endl;
        std::string body = R"({"ok":false})";
        std::string resp = buildHttpResponse(200, body, "application/json");
        req.sendFn(req.link_id, resp);
        
        });
}

void PollUART()
{
    std::string incomingBytes;
        uart.ReadFromUart(incomingBytes);
        if (incomingBytes.empty()) {
            
            return;
        }
        handler->feed(incomingBytes);
}

void Scan()
{
    Ethernet_header_t head;
    head.package_type = START_RECORDING;
    head.packageLength=sizeof(head);
    ethernet->Send(&head, sizeof(head));
}

int main() {
    std::cout << "Starting up" << std::endl;
    uart.Start();
    StartWifiUart();
    ethernet = new Ethenet(192,168,1,5, false);

    init_SPI(&spi_instance, spi_config);
    mcp1 = new MCP4261(SPI_CS_AMP, &spi_instance, 2, &GPIOs, XPAR_XGPIO_0_BASEADDR, HW_GPIO_OFFSET_PYNQ);
    mcp2 = new MCP4261(SPI_CS_VCOM, &spi_instance, 2, &GPIOs, XPAR_XGPIO_0_BASEADDR, HW_GPIO_OFFSET_PYNQ);

    while (1) {

        

        PollUART();
        ethernet->Poll();
        if(ethernet->HasFinishedData())
        {
            switch (ethernet->GetCurrentPackageType())
            {
                case ACK:
                    std::cout << "Got ack from ethernet\n";
                break;
                case START_RECORDING:
                    std::cout << "Got start recording from ethernet\n";
                break;
                case RECORDED_WAVEFORM:
                    std::cout << "Got recorded waveform of size" << ethernet->GetPayloadSize() << " from ethernet\n";
                    
                    storedSignal.reserve(storedSignal.size() + 32);
                    storedSignal.insert(storedSignal.end(), ethernet->GetPayload() + 5, ethernet->GetPayload() + 37);
                    std::cout << "Stored signal is now " << storedSignal.size() << std::endl;
                    ethernet->DoneWithPayload();
                    if (storedSignal.size() < DATALENGTH)
                    {
                        Scan();
                    }
                    else {
                        Ethernet_header_t header;
                        header.package_type = ACK;
                        header.packageLength = sizeof(header);
                        ethernet->Send(&header, sizeof(header));
                        std::string payload = std::string(storedSignal.data(), storedSignal.size());
                        handler->sendHttpRequest(
                            "http://192.168.4.2/api/registerRawSignals",
                            HttpMethod::POST,
                            payload,
                            "application/octet-stream",             // binary MIME type
                            [](const HttpResponse& resp) {
                                std::cout << "HTTP " << resp.status << "\n";
                                std::cout << "Body: " << resp.body.substr(0, 200) << "...\n"; // avoid printing MBs
                                if (resp.status != 200)
                                    std::cout << "Server error or no response\n";
                            });
                    }
                    
                break;
                case TRAININGDATA:
                    std::cout << "Got start trainingdata from ethernet\n";
                break;
                case START_INFERNECE:
                    std::cout << "Got start infernece from ethernet\n";
                break;
                case RESULTS:
                    std::cout << "Got results from ethernet\n";
                break;
                default: break;
            }
            ethernet->DoneWithPayload();
        }
        usleep(1000);
            
    }
    delete handler;
    delete mcp1;
    return EXIT_SUCCESS;
}
