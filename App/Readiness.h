#pragma once
#include <cstdint>
namespace Simulator {
enum class PortState { Waiting, Ready, Recovering, StartupTimeout, RecoveryTimeout };
// A started, owned COM interface can be opened to wake an idle USB device.
// Current D0/configuration flags are diagnostics, not a prerequisite for opening.
class PortReadiness {
    std::uint64_t started_,missing_=0;
    bool enumerated_=false,missing_active_=false;
public:
    explicit PortReadiness(std::uint64_t started):started_(started){}
    PortState Observe(bool present,std::uint64_t now) noexcept {
        if(present){enumerated_=true;missing_active_=false;return PortState::Ready;}
        if(!enumerated_)return now-started_>=15000?PortState::StartupTimeout:PortState::Waiting;
        if(!missing_active_){missing_=now;missing_active_=true;}
        return now-missing_>=15000?PortState::RecoveryTimeout:PortState::Recovering;
    }
};
}
