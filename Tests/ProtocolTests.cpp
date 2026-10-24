#include "Protocol.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace Simulator;
unsigned checks=0;
void Check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}
std::vector<Bytes> Send(Engine& e,Bytes data){return e.Feed(WithCrc(std::move(data)),1);}
int main()try{
    Check(Crc16({1,3,0,0,0,10})==0xCDC5,"Known Modbus CRC");
    Engine e;auto r=Send(e,{1,3,0,0,0,1});Check(r.size()==1&&r[0]==Bytes({1,3,2,12,228,0xBD,0x0F}),"Read default register");
    r=Send(e,{1,6,0,1,0,42});Check(r.size()==1&&e.Snapshot().holding[1]==42,"Write single register");
    r=Send(e,{1,16,0,2,0,2,4,0,7,0,8});Check(r.size()==1&&e.Snapshot().holding[3]==8,"Write multiple registers");
    const auto before=e.Snapshot().holding;r=Send(e,{1,16,0,2,0,2,2,0,9});Check(!r.empty()&&r[0][2]==3&&e.Snapshot().holding==before,"Invalid multi-write is atomic");
    r=Send(e,{1,4,0,0,0,1});Check(r[0]==WithCrc({1,4,2,12,228}),"Input registers remain separate from holding writes");
    r=Send(e,{1,2,0,0,0,8});Check(r[0]==WithCrc({1,2,1,0}),"Discrete inputs remain separate from coils");
    r=Send(e,{1,15,0,0,0,9,2,0x81,1});Check(r.size()==1&&e.Snapshot().coils[0]&&e.Snapshot().coils[7]&&e.Snapshot().coils[8],"Packed coils");
    r=Send(e,{1,1,0,0,0,9});Check(r[0]==WithCrc({1,1,2,0x81,1}),"Read packed coils");
    r=Send(e,{1,3,0,127,0,2});Check(r[0][2]==2,"Address bound");
    r=Send(e,{1,3,0,0,0,0});Check(r[0][2]==3,"Quantity bound");
    r=Send(e,{1,5,0,1,0,1});Check(r[0][2]==3,"Coil value bound");
    Check(Send(e,{2,6,0,0,0,9}).empty()&&e.Snapshot().holding[0]==3300,"Other unit never writes");
    Check(Send(e,{0,6,0,0,0,9}).empty()&&e.Snapshot().holding[0]==9,"Broadcast writes without reply");
    Check(Send(e,{0,3,0,0,0,1}).empty(),"Broadcast reads never respond");
    auto bad=WithCrc({1,6,0,0,0,90});bad.back()^=1;Check(e.Feed(bad,2).empty()&&e.Snapshot().holding[0]==9,"CRC failure never writes");
    Engine split;const auto request=WithCrc({1,3,0,0,0,1});for(std::size_t i=0;i<request.size();++i){r=split.Feed({request[i]},i);Check(r.size()==(i==7?1:0),"Split transport packets");}
    Bytes combined=request;combined.insert(combined.end(),request.begin(),request.end());Check(split.Feed(combined,9).size()==2,"Coalesced requests");
    auto unsupported=WithCrc({1,0x45});Check(split.Feed(unsupported,10).empty(),"Unknown frame waits for idle");r=split.Idle(31);Check(r.size()==1&&r[0][1]==0xC5&&r[0][2]==1,"Unknown function exception");
    Engine scpi(Protocol::Scpi);const auto command=[&](const std::string& text){return scpi.Feed(Bytes(text.begin(),text.end()),0);};
    r=command("*IDN?\r\n*OPC?\n");Check(r.size()==2,"SCPI CRLF and multiple commands");
    command("SOUR:VOLT 12.345\n");Check(scpi.Snapshot().holding[0]==12345,"SCPI numeric setting");
    r=command("SOUR:VOLT?\n");Check(std::string(r[0].begin(),r[0].end())=="12.345\n","SCPI setting query");
    command("SOUR:VOLT nan\n");command("OUTP ON\n");r=command("OUTP?\nSYST:ERR?\n");Check(r.size()==2&&r[0]==Bytes({'1','\n'})&&r[1][0]=='-',"SCPI errors and output state");
    command(std::string(400,'x')+"\n");Check(!command("*OPC?\n").empty(),"SCPI recovers after bounded overflow");
    std::mt19937 random(48291);for(unsigned n=0;n<50000;++n){Bytes fuzz(random()%280);for(auto& b:fuzz)b=static_cast<std::uint8_t>(random());const auto response=e.Feed(fuzz,n*25u);for(const auto& frame:response)Check(frame.size()<=256&&Crc16(frame)==0,"Fuzz response invariant");scpi.Feed(fuzz,n);Check(e.Snapshot().holding.size()==128,"Bounded model stress");}
    std::cout<<checks<<" protocol checks passed; no driver or COM port opened.\n";return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
