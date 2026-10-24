#pragma once
#include "Protocol.h"
#include <memory>
#include <string>
#include <vector>
namespace Simulator {
struct Status {
    bool connected=false, ready=false;
    std::wstring port, message=L"Ready. Choose a protocol and create a virtual instrument; no physical port is opened.";
    State state;
    std::vector<std::wstring> log;
};
class Session {
public:
    Session(); ~Session();
    void Start(Protocol protocol,unsigned unit);
    void SetRegister(unsigned index,std::uint16_t value);
    void Stop();
    Status Read();
private: struct Impl; std::unique_ptr<Impl> impl_;
};
int SmokeTest(const std::wstring& report);
}
