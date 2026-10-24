#include "Readiness.h"
#include <iostream>
#include <stdexcept>
int main()try{
    using namespace Simulator;unsigned count=0;
    auto check=[&](bool b){++count;if(!b)throw std::runtime_error("Port lifecycle policy failed.");};
    PortReadiness startup(100);check(startup.Observe(false,100)==PortState::Waiting);check(startup.Observe(false,15099)==PortState::Waiting);check(startup.Observe(false,15100)==PortState::StartupTimeout);
    PortReadiness session(0);check(session.Observe(true,200)==PortState::Ready);
    // Being idle for minutes does not consume a later re-enumeration grace period.
    check(session.Observe(false,200000)==PortState::Recovering);check(session.Observe(false,214999)==PortState::Recovering);check(session.Observe(true,214999)==PortState::Ready);
    // Every recovered COM interface starts a fresh recovery episode.
    check(session.Observe(false,300000)==PortState::Recovering);check(session.Observe(false,315000)==PortState::RecoveryTimeout);check(session.Observe(true,315001)==PortState::Ready);
    check(session.Observe(false,400000)==PortState::Recovering);check(session.Observe(true,400001)==PortState::Ready);
    PortReadiness delayed(0);check(delayed.Observe(true,20000)==PortState::Ready);
    std::cout<<count<<" startup/re-enumeration policy checks passed; no driver or COM access.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
