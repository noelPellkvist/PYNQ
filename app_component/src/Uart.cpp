#include "Uart.hpp"
#include <iostream>

#include "xparameters.h"
#include "xuartlite_l.h"
#include "sleep.h"

#include "xuartlite_l.h"
#define UARTLITE_BASE   XPAR_XUARTLITE_0_BASEADDR

static inline void uartlite_send_byte(u8 b) {
    // Wait if TX FIFO is full, then send one byte
    while (XUartLite_IsTransmitFull(UARTLITE_BASE)) { }
    XUartLite_SendByte(UARTLITE_BASE, b);
}

static void uartlite_send_buf(const u8* data, unsigned len) {
    for (unsigned i = 0; i < len; ++i) {
        uartlite_send_byte(data[i]);
    }
}

static void strip_echo_if_present(std::string& buf, const std::string& cmd_line) {
    // cmd_line should be without trailing \r\n (e.g. "AT+CWMODE=2")
    auto pos = buf.find("\r\n");
    if (pos != std::string::npos) {
        // First line received
        std::string first = buf.substr(0, pos);
        if (first == cmd_line) {
            buf.erase(0, pos + 2); // drop the echo line + CRLF
        }
    }
}

bool Uart::SendAndWait(const std::string& cmd_with_crlf,
                       std::initializer_list<const char*> expected_any,
                       unsigned timeout_ms,
                       std::string* out_all)
{
    if (out_all) out_all->clear();

    // Clear pending bytes before sending
    std::string rx;
    ReadFromUart(rx);

    WriteToUart(cmd_with_crlf);

    // Build a copy of the command line without CRLF for echo stripping
    std::string cmd_line = cmd_with_crlf;
    if (!cmd_line.empty() && cmd_line.back() == '\n') cmd_line.pop_back();
    if (!cmd_line.empty() && cmd_line.back() == '\r') cmd_line.pop_back();

    // Poll until we see any expected token or we time out
    const unsigned step_ms = 2;
    unsigned waited = 0;
    bool echo_stripped = false;

    while (waited < timeout_ms) {
        ReadFromUart(rx);

        if (!echo_stripped) {
            strip_echo_if_present(rx, cmd_line);
            // only try once; after the first CRLF we either stripped or not
            if (rx.find("\r\n") != std::string::npos) echo_stripped = true;
        }

        for (auto tok : expected_any) {
            if (rx.find(tok) != std::string::npos) {
                if (out_all) *out_all = rx;
                return true;
            }
        }

        usleep(step_ms * 1000);
        waited += step_ms;
    }

    if (out_all) *out_all = rx;
    return false;
}

void Uart::Start()
{
xil_printf("Starting serial; flushing RX...\r\n");
    std::string dump; ReadFromUart(dump);

    // 1) Disable echo so later responses are clean
    std::string rx;
    if (!SendAndWait("ATE0\r\n", {"OK"}, 500, &rx)) {
        xil_printf("Warn: ATE0 timeout; rx:\r\n%s\r\n", rx.c_str());
    }

    // 2) Your setup sequence (allow “no change” where common)
    struct Step { const char* cmd; std::initializer_list<const char*> expect; unsigned to_ms; };
    std::vector<Step> steps = {
        { "AT\r\n",                       {"OK"},                                  500 },
        { "AT+CWMODE=2\r\n",              {"OK","no change"},                      1500 },
        { "AT+CWSAP=\"PYNQNET\",\"12345678\",5,3\r\n", {"OK"},                     5000 },
        { "AT+CIFSR\r\n",                 {"OK"},                                  1500 }, // will also print IP
        { "AT+CIPMUX=1\r\n",              {"OK","link is builded","no change"},    1500 },
        { "AT+CIPSERVER=1,80\r\n",        {"OK","no change","link is builded"},    3000 },
    };

    for (auto& s : steps) {
        rx.clear();
        bool ok = SendAndWait(s.cmd, s.expect, s.to_ms, &rx);
        //xil_printf(">> %s", s.cmd);
        //xil_printf("<< %s\r\n", rx.c_str());
        if (!ok) {
            xil_printf("BOOTCMD FAIL %s (wanted one of: ", s.cmd);
            for (auto e : s.expect) xil_printf("'%s' ", e);
            xil_printf(")\r\n");
            // You can decide to return or continue
        }
        usleep(100*1000);
    }

    xil_printf("WiFi init done\r\n");

}

Uart::Uart() {
    }

void Uart::WriteToUart(const std::string& msg)
{
    uartlite_send_buf(reinterpret_cast<const u8*>(msg.c_str()), msg.size());
}

void Uart::ReadFromUart(std::string& msg)
{
    while (!XUartLite_IsReceiveEmpty(UARTLITE_BASE)) {
        unsigned char c = XUartLite_RecvByte(UARTLITE_BASE);
        msg.push_back(static_cast<char>(c)); 
    }
}