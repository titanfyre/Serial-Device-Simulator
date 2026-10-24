#include "Protocol.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Simulator {
namespace {
std::uint16_t Word(const Bytes& d,std::size_t i){return static_cast<std::uint16_t>((d.at(i)<<8)|d.at(i+1));}
void WordOut(Bytes& d,std::uint16_t v){d.push_back(static_cast<std::uint8_t>(v>>8));d.push_back(static_cast<std::uint8_t>(v));}
bool Range(unsigned start,unsigned count){return count && start<128 && count<=128-start;}
std::string Canonical(std::string s){const auto first=s.find_first_not_of(" \t");if(first==std::string::npos)return {};s=s.substr(first,s.find_last_not_of(" \t")-first+1);for(auto& c:s)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');if(!s.empty()&&s.front()==':')s.erase(0,1);return s;}
Bytes Text(std::string s){s+='\n';return Bytes(s.begin(),s.end());}
std::string Voltage(std::uint16_t v){std::ostringstream s;s.imbue(std::locale::classic());s<<std::fixed<<std::setprecision(3)<<v/1000.0;return s.str();}
}
std::uint16_t Crc16(const Bytes& d){std::uint16_t crc=0xFFFF;for(auto b:d){crc^=b;for(unsigned n=0;n<8;++n)crc=static_cast<std::uint16_t>((crc>>1)^((crc&1)?0xA001:0));}return crc;}
Bytes WithCrc(Bytes d){const auto crc=Crc16(d);d.push_back(static_cast<std::uint8_t>(crc));d.push_back(static_cast<std::uint8_t>(crc>>8));return d;}
Engine::Engine(Protocol p,unsigned unit):protocol_(p),unit_(unit){if(unit<1||unit>247)throw std::invalid_argument("Unit address must be 1..247.");pending_.reserve(256);state_.holding[0]=state_.inputs[0]=3300;}
void Engine::SetRegister(unsigned i,std::uint16_t v){if(i>=128)throw std::out_of_range("Register index must be 0..127.");state_.holding[i]=v;state_.inputs[i]=v;}
Bytes Engine::Frame(const Bytes& d){
    if(d.size()<4||d.size()>256||Crc16(d)!=0){++state_.rejected;return {};}
    if(d[0]!=unit_&&d[0]!=0){++state_.ignored;return {};}
    ++state_.requests;const bool broadcast=d[0]==0;const auto fn=d[1];
    const auto reply=[&](Bytes out){if(broadcast){return Bytes{};}++state_.responses;return WithCrc(std::move(out));};
    const auto fail=[&](std::uint8_t code){++state_.rejected;return reply({d[0],static_cast<std::uint8_t>(fn|0x80),code});};
    if(fn!=1&&fn!=2&&fn!=3&&fn!=4&&fn!=5&&fn!=6&&fn!=15&&fn!=16)return fail(1);
    if(d.size()<8)return fail(3);
    const auto start=Word(d,2),value=Word(d,4);
    if(fn>=1&&fn<=4){
        if(broadcast){++state_.ignored;return {};}
        if(d.size()!=8||!value||value>(fn<=2?2000:125))return fail(3);
        if(!Range(start,value))return fail(2);
        Bytes out{d[0],fn};
        if(fn<=2){out.push_back(static_cast<std::uint8_t>((value+7)/8));out.resize(3+(value+7)/8,0);const auto& bits=fn==1?state_.coils:state_.discrete;for(unsigned n=0;n<value;++n)if(bits[start+n])out[3+n/8]|=static_cast<std::uint8_t>(1u<<(n%8));}
        else{out.push_back(static_cast<std::uint8_t>(value*2));const auto& regs=fn==3?state_.holding:state_.inputs;for(unsigned n=0;n<value;++n)WordOut(out,regs[start+n]);}
        return reply(std::move(out));
    }
    if(fn==5||fn==6){if(d.size()!=8)return fail(3);if(start>=128)return fail(2);if(fn==5){if(value!=0&&value!=0xFF00)return fail(3);state_.coils[start]=value!=0;}else state_.holding[start]=value;return reply(Bytes(d.begin(),d.begin()+6));}
    if(d.size()<9||!value||value>(fn==15?1968:123))return fail(3);
    const unsigned count=fn==15?(value+7)/8:value*2;
    if(d[6]!=count||d.size()!=9+count)return fail(3);
    if(!Range(start,value))return fail(2);
    for(unsigned n=0;n<value;++n){if(fn==15)state_.coils[start+n]=(d[7+n/8]&(1u<<(n%8)))!=0;else state_.holding[start+n]=Word(d,7+n*2);}
    return reply(Bytes(d.begin(),d.begin()+6));
}
void Engine::Error(const std::string& e){++state_.rejected;if(errors_.size()<16)errors_.push_back(e);}
Bytes Engine::Scpi(const std::string& raw){
    const auto s=Canonical(raw);if(s.empty())return {}; ++state_.requests;
    const auto reply=[&](const std::string& text){++state_.responses;return Text(text);};
    if(s=="*IDN?")return reply("TitanCSA,Serial-Device-Simulator,VIRTUAL,0.1.0");
    if(s=="*OPC?")return reply("1");
    if(s=="*CLS"){errors_.clear();return {};}
    if(s=="*RST"){state_.holding.fill(0);state_.inputs.fill(0);state_.holding[0]=state_.inputs[0]=3300;state_.coils.fill(false);state_.discrete.fill(false);errors_.clear();return {};}
    if(s=="SYST:ERR?"||s=="SYSTEM:ERROR?"){const auto text=errors_.empty()?"0,\"No error\"":errors_.front();if(!errors_.empty())errors_.pop_front();return reply(text);}
    if(s=="MEAS:VOLT?"||s=="MEASURE:VOLTAGE?")return reply(Voltage(state_.inputs[0]));
    if(s=="SOUR:VOLT?"||s=="SOURCE:VOLTAGE?")return reply(Voltage(state_.holding[0]));
    if(s=="OUTP?"||s=="OUTPUT?")return reply(state_.coils[0]?"1":"0");
    if(s=="OUTP ON"||s=="OUTP 1"||s=="OUTPUT ON"||s=="OUTPUT 1"){state_.coils[0]=true;return {};}
    if(s=="OUTP OFF"||s=="OUTP 0"||s=="OUTPUT OFF"||s=="OUTPUT 0"){state_.coils[0]=false;return {};}
    const auto space=s.find(' ');if(space!=std::string::npos&&(s.substr(0,space)=="SOUR:VOLT"||s.substr(0,space)=="SOURCE:VOLTAGE")){
        auto number=Canonical(s.substr(space+1));if(!number.empty()&&number.front()=='+')number.erase(0,1);double value=0;
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||!std::isfinite(value)){Error("-104,\"Data type error\"");return {};}
        if(value<0||value>65.535){Error("-222,\"Data out of range\"");return {};}
        state_.holding[0]=static_cast<std::uint16_t>(std::lround(value*1000));return {};
    }
    Error("-113,\"Undefined header\"");return {};
}
std::vector<Bytes> Engine::Idle(std::uint64_t now){std::vector<Bytes> result;if(protocol_==Protocol::ModbusRtu&&has_time_&&(now<last_||now-last_>=20)&&!pending_.empty()){auto out=Frame(pending_);pending_.clear();if(!out.empty())result.push_back(std::move(out));}return result;}
std::vector<Bytes> Engine::Feed(const Bytes& data,std::uint64_t now){
    auto result=Idle(now);last_=now;has_time_=true;
    for(auto b:data){
        if(protocol_==Protocol::Scpi){
            if(b=='\r'||b=='\n'){if(!discard_line_&&!pending_.empty()){const auto out=Scpi(std::string(pending_.begin(),pending_.end()));if(!out.empty())result.push_back(out);}pending_.clear();discard_line_=false;continue;}
            if(discard_line_)continue;
            if((b<32&&b!='\t')||b>126||pending_.size()>=255){Error("-363,\"Input buffer overrun or invalid byte\"");pending_.clear();discard_line_=true;continue;}pending_.push_back(b);continue;
        }
        if(pending_.size()>=256){++state_.rejected;pending_.clear();}
        pending_.push_back(b);
        if(pending_.size()<2)continue;
        const auto fn=pending_[1];std::size_t expected=0;
        if((fn>=1&&fn<=6))expected=8;
        else if((fn==15||fn==16)&&pending_.size()>=7)expected=9u+pending_[6];
        if(expected>256){++state_.rejected;pending_.clear();continue;}
        if(expected&&pending_.size()==expected){const auto out=Frame(pending_);pending_.clear();if(!out.empty())result.push_back(out);}
    }
    return result;
}
}
