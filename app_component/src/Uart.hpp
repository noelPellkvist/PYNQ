#pragma once

#include <string>
#include <vector>
#include <utility>

class Uart 
{
    public:
        Uart();
        ~Uart() = default;

        void Start();

        void WriteToUart(const std::string& msg);
        void ReadFromUart(std::string& msg);
        bool SendAndWait(const std::string& cmd_with_crlf,
                       std::initializer_list<const char*> expected_any,
                       unsigned timeout_ms,
                       std::string* out_all);

    private:
        int RunUartRoutine(std::vector<std::pair<std::string, std::string>> commands);
};