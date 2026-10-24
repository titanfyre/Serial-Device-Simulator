#include "Session.h"
#include <Windows.h>
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv)try{
    if(argc==2&&std::wstring(argv[1])==L"--self-test"){
        Simulator::Engine engine;const auto response=engine.Feed(Simulator::WithCrc({1,3,0,0,0,1}),0);
        if(response.size()!=1||response[0]!=Simulator::WithCrc({1,3,2,12,228}))return 1;
        std::cout<<"Offline simulator check passed; no COM port or driver opened.\n";return 0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--smoke-test")return Simulator::SmokeTest(argv[2]);
    std::cout<<"Serial Device Simulator - virtual Modbus RTU / limited SCPI instruments\n"
        <<"  --self-test               Offline model check, no driver access\n"
        <<"  --smoke-test report.json  Live owned-virtual-COM roundtrips; administrator required\n";
    return argc==1?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
