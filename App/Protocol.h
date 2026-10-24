#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace Simulator {
using Bytes = std::vector<std::uint8_t>;
enum class Protocol { ModbusRtu, Scpi };
std::uint16_t Crc16(const Bytes& data);
Bytes WithCrc(Bytes data);
struct State {
    std::array<std::uint16_t,128> holding{}, inputs{};
    std::array<bool,128> coils{}, discrete{};
    std::uint64_t requests=0, responses=0, rejected=0, ignored=0;
};
class Engine {
public:
    explicit Engine(Protocol protocol=Protocol::ModbusRtu, unsigned unit=1);
    std::vector<Bytes> Feed(const Bytes& data, std::uint64_t milliseconds);
    std::vector<Bytes> Idle(std::uint64_t milliseconds);
    const State& Snapshot() const noexcept { return state_; }
    void SetRegister(unsigned index, std::uint16_t value);
private:
    Protocol protocol_; unsigned unit_; State state_;
    Bytes pending_; std::uint64_t last_=0; bool has_time_=false, discard_line_=false;
    std::deque<std::string> errors_;
    Bytes Frame(const Bytes& frame);
    Bytes Scpi(const std::string& line);
    void Error(const std::string& text);
};
}
