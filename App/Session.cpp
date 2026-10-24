#include "Session.h"
#include "Cdc.h"
#include "Readiness.h"
#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <algorithm>

namespace Simulator {
namespace {
std::wstring Wide(const std::string& s){if(s.empty())return {};UINT page=CP_UTF8;int count=MultiByteToWideChar(page,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!count){page=CP_ACP;count=MultiByteToWideChar(page,0,s.data(),static_cast<int>(s.size()),nullptr,0);}if(!count)return {s.begin(),s.end()};std::wstring text(static_cast<std::size_t>(count),0);MultiByteToWideChar(page,0,s.data(),static_cast<int>(s.size()),text.data(),count);return text;}
std::string Utf8(const std::wstring& s){if(s.empty())return {};const int count=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(count<=0)return "Text conversion failed.";std::string text(static_cast<std::size_t>(count),0);WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),text.data(),count,nullptr,nullptr);return text;}
std::string Json(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){const char hex[]="0123456789ABCDEF";out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}out+='"';return out;}
std::uint64_t Now(){return GetTickCount64();}
template<class T,class=void>struct Demo{static bool Expired(const T&){return false;}};
template<class T>struct Demo<T,std::void_t<decltype(std::declval<const T&>().trial_expired)>>{static bool Expired(const T& s){return s.trial_expired;}};
std::wstring FindPort(const std::string& serial){
    const GUID ports={0x4d36e978,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
    const auto set=SetupDiGetClassDevsW(&ports,nullptr,nullptr,DIGCF_PRESENT);if(set==INVALID_HANDLE_VALUE)return {};
    struct Close {HDEVINFO h;~Close(){SetupDiDestroyDeviceInfoList(h);}} close{set};
    const auto suffix=L"\\"+Wide(serial);SP_DEVINFO_DATA node{sizeof(node)};
    for(DWORD index=0;SetupDiEnumDeviceInfo(set,index,&node);++index){
        ULONG status=0,problem=0;if(CM_Get_DevNode_Status(&status,&problem,node.DevInst,0)!=CR_SUCCESS||problem||(status&DN_STARTED)==0)continue;
        DEVINST current=node.DevInst;bool ours=false;
        for(unsigned level=0;level<8;++level){wchar_t id[MAX_DEVICE_ID_LEN]{};if(CM_Get_Device_IDW(current,id,MAX_DEVICE_ID_LEN,0)!=CR_SUCCESS)break;
            const std::wstring text=id;if(text.size()>=suffix.size()&&_wcsicmp(text.c_str()+text.size()-suffix.size(),suffix.c_str())==0){ours=true;break;}DEVINST parent=0;if(CM_Get_Parent(&parent,current,0)!=CR_SUCCESS)break;current=parent;}
        if(!ours)continue;
        const auto key=SetupDiOpenDevRegKey(set,&node,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);if(key==INVALID_HANDLE_VALUE)continue;
        wchar_t name[32]{};DWORD size=sizeof(name),type=0;const auto result=RegQueryValueExW(key,L"PortName",nullptr,&type,reinterpret_cast<BYTE*>(name),&size);RegCloseKey(key);
        if(result!=ERROR_SUCCESS||type!=REG_SZ||size<10||size>=sizeof(name))continue;
        const std::wstring port=name;if(port.size()<4||port.size()>8||port.substr(0,3)!=L"COM"||port[3]<'1'||port[3]>'9'||!std::all_of(port.begin()+3,port.end(),[](wchar_t c){return c>='0'&&c<='9';}))continue;
        return port;
    }return {};
}
}
struct Session::Impl {
    std::mutex mutex;std::thread worker;std::atomic<bool> stop{false},running{false};
    std::deque<std::pair<unsigned,std::uint16_t>> writes;Status snapshot;
    void Publish(const Status& status){std::lock_guard<std::mutex> lock(mutex);snapshot=status;}
    void Run(Protocol protocol,unsigned unit) noexcept {
        struct RunningGuard{std::atomic<bool>& running;~RunningGuard(){running=false;}} active{running};
        Vdevice::Device device;Status status;std::unique_ptr<Engine> model;Cdc::LineState line;Cdc::ByteQueue queue;
        std::string phase="create_device";
        const auto log=[&](const std::wstring& s){status.log.push_back(s);if(status.log.size()>48)status.log.erase(status.log.begin());};
        try {
            model=std::make_unique<Engine>(protocol,unit);auto& engine=*model;
            status.message=L"Creating an owned virtual USB instrument...";Publish(status);
            GUID id{};if(CoCreateGuid(&id)!=S_OK)throw std::runtime_error("Could not create a unique USB serial identity.");
            wchar_t text[40]{};StringFromGUID2(id,text,40);std::string serial="TSIM";for(auto c:std::wstring(text))if((c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f'))serial+=static_cast<char>(c);
            device=Vdevice::Create_Device(Cdc::Configuration(serial));if(!device)throw std::runtime_error(device.message+" (Windows "+std::to_string(device.error)+")");
            status.connected=true;status.message=L"Waiting for Windows usbser.sys to enumerate this instrument...";Publish(status);
            PortReadiness readiness(Now());auto check=std::uint64_t{0};Bytes pendingNotification;std::wstring lastPort;
            const auto enqueue=[&](const std::vector<Bytes>& frames){for(const auto& frame:frames){if(!queue.Push(frame))throw std::runtime_error("Response queue full; the instrument stopped without silently dropping replies.");log(L"TX  "+Wide(Cdc::Hex(frame)));}};
            while(!stop.load(std::memory_order_relaxed)){
                phase="process_device_events";
                {std::deque<std::pair<unsigned,std::uint16_t>> edits;{std::lock_guard<std::mutex> lock(mutex);edits.swap(writes);}for(const auto& edit:edits)engine.SetRegister(edit.first,edit.second);}
                const auto output=Vdevice::Get_Output(10);
                if(output){
                    if(output.device_id!=device.Id())throw std::runtime_error("Unexpected device ownership in this session.");
                    if(output.kind==Vdevice::Output_Kind::Control){const auto reply=Cdc::HandleControl(line,output.setup,output.data);const auto sent=Vdevice::Respond_Output(output,reply.data,reply.stall);if(!sent)throw std::runtime_error("CDC control reply failed: "+sent.message);if(reply.notification)pendingNotification=Cdc::SerialNotification(line);}
                    else if(output.kind==Vdevice::Output_Kind::Data&&output.endpoint==0x02){log(L"RX  "+Wide(Cdc::Hex(output.data)));enqueue(engine.Feed(output.data,Now()));}
                }else if(!output.timed_out&&output.error!=ERROR_TIMEOUT&&output.error!=ERROR_NO_MORE_ITEMS&&output.error!=ERROR_NO_DATA)throw std::runtime_error("Device event failed: "+output.message);
                enqueue(engine.Idle(Now()));
                phase="send_responses";
                if(!pendingNotification.empty()){
                    const auto notify=Vdevice::Send_Input(device,0x81,pendingNotification);
                    if(notify)pendingNotification.clear();
                    else if(notify.error!=ERROR_BUSY&&notify.error!=ERROR_NOT_READY&&notify.error!=ERROR_RETRY)throw std::runtime_error("CDC notification failed: "+notify.message);
                }
                for(unsigned n=0;n<4;++n){const auto* bytes=queue.Front();if(!bytes)break;const auto sent=Vdevice::Send_Input(device,0x82,*bytes);if(!sent){if(sent.error==ERROR_BUSY||sent.error==ERROR_NOT_READY||sent.error==ERROR_RETRY)break;throw std::runtime_error("Response send failed: "+sent.message);}queue.Pop();}
                if(Now()-check>=200){phase="enumerate_owned_com";check=Now();const auto info=Vdevice::Get_Info();if(!info)throw std::runtime_error("Vdevice status query failed: "+info.message+" (Windows "+std::to_string(info.error)+")");
                    if(info.removing||info.quiescing)throw std::runtime_error("Vdevice is being removed or quiesced.");
                    if(Demo<Vdevice::Info>::Expired(info))throw std::runtime_error("The Vdevice demo allowance expired; restart Windows for another allowance.");
                    const auto found=std::find_if(info.devices.begin(),info.devices.end(),[&](const auto& d){return d.id==device.Id();});if(found==info.devices.end()||found->error)throw std::runtime_error("The virtual instrument disappeared or reported a driver error.");
                    status.port=FindPort(serial);const auto portState=readiness.Observe(!status.port.empty(),Now());
                    status.ready=portState==PortState::Ready;
                    if(status.ready){lastPort=status.port;status.message=L"Ready on "+status.port+L". Open this exact port in your client. State is simulated; no physical equipment is controlled.";
                        if(!found->powered)status.message+=L" USB is idle/suspended; opening the port or communicating can resume it.";
                    }
                    else {
                        const std::string flags=" USB status: configured="+std::string(found->configured?"true":"false")+", powered="+std::string(found->powered?"true":"false")+".";
                        if(portState==PortState::StartupTimeout)throw std::runtime_error("The owned usbser.sys COM interface did not start within 15 seconds."+flags+" Inspect Device Manager and use the matching Vdevice setup.");
                        if(portState==PortState::RecoveryTimeout)throw std::runtime_error("The previously enumerated COM interface did not return within a new 15-second recovery period."+flags+" Last port: "+Utf8(lastPort)+". Inspect device removal/reset diagnostics.");
                        status.message=portState==PortState::Recovering?L"The owned COM interface is temporarily unavailable. Waiting up to 15 seconds for Windows to re-enumerate it.":L"Waiting for the exact owned usbser.sys COM interface to start...";
                    }
                    status.state=engine.Snapshot();Publish(status);
                }
            }
            status.message=L"Stopping. Removing the owned virtual instrument...";
        }catch(const std::exception& e){status.message=L"Stopped ["+Wide(phase)+L"]: "+Wide(e.what());}catch(...){status.message=L"Stopped ["+Wide(phase)+L"] after an unexpected operation error.";}
        status.ready=false;Publish(status); // Preserve a terminal worker error even if cleanup stalls.
        if(device){try{const auto removed=Vdevice::Delete_Device(device);if(!removed)status.message+=L" Device removal reported: "+Wide(removed.message);else if(status.message.rfind(L"Stopping.",0)==0)status.message=L"Stopped. The owned virtual instrument has been removed.";}catch(const std::exception& e){status.message+=L" Device cleanup failed: "+Wide(e.what());}}
        status.connected=status.ready=false;status.port.clear();if(model)status.state=model->Snapshot();Publish(status);
    }
};
Session::Session():impl_(std::make_unique<Impl>()){}
Session::~Session(){Stop();}
void Session::Start(Protocol p,unsigned unit){if(impl_->running.load())throw std::runtime_error("An instrument is already starting or running. Stop it before creating another.");Stop();if(unit<1||unit>247)throw std::invalid_argument("Unit address must be 1..247.");{std::lock_guard<std::mutex> lock(impl_->mutex);impl_->snapshot={};impl_->snapshot.message=L"Creating an owned virtual USB instrument...";impl_->writes.clear();}impl_->stop=false;impl_->running=true;try{impl_->worker=std::thread([this,p,unit]{impl_->Run(p,unit);});}catch(...){impl_->running=false;throw;}}
void Session::SetRegister(unsigned index,std::uint16_t v){if(index>=128)throw std::invalid_argument("Register index must be 0..127.");std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->writes.size()>=128)throw std::runtime_error("Edit queue full.");impl_->writes.emplace_back(index,v);}
void Session::Stop(){impl_->stop=true;if(impl_->worker.joinable())impl_->worker.join();}
Status Session::Read(){std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->snapshot;}
int SmokeTest(const std::wstring& report){
    bool success=false;std::string phase="start",detail="Virtual COM test has not completed.";std::vector<std::string> steps;
    const auto save=[&](bool completed){std::ofstream file(std::filesystem::path(report),std::ios::binary|std::ios::trunc);file<<"{\"success\":"<<(success?"true":"false")<<",\"completed\":"<<(completed?"true":"false")<<",\"scope\":\"owned virtual CDC Modbus and SCPI roundtrips only\",\"phase\":"<<Json(phase)<<",\"detail\":"<<Json(detail)<<",\"steps\":[";for(std::size_t n=0;n<steps.size();++n){if(n)file<<',';file<<Json(steps[n]);}file<<"]}\n";file.flush();return static_cast<bool>(file);};
    const auto checkpoint=[&](const std::string& next){phase=next;if(steps.size()<64)steps.push_back(phase);if(!save(false))throw std::runtime_error("Could not write the requested progress report.");};
    if(!save(false))return 2;
    try{for(const auto protocol:{Protocol::ModbusRtu,Protocol::Scpi}){
        const std::string name=protocol==Protocol::ModbusRtu?"modbus":"scpi";
        Session session;
        try{
            checkpoint(name+".create_device");session.Start(protocol,1);checkpoint(name+".enumerate_owned_com");
            Status state;std::wstring previousMessage;const auto until=Now()+20000;
            do{
                state=session.Read();
                if(state.message!=previousMessage){previousMessage=state.message;detail=Utf8(state.message);if(steps.size()<64)steps.push_back(name+": "+detail);if(!save(false))throw std::runtime_error("Could not update the requested progress report.");}
                if(state.ready)break;
                if(state.message.rfind(L"Stopped",0)==0)throw std::runtime_error("Instrument worker failed: "+Utf8(state.message));
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }while(Now()<until);
            if(!state.ready)throw std::runtime_error("Timed out waiting for the exact owned COM interface. Last worker state: "+Utf8(state.message));
            checkpoint(name+".open_com");const auto path=L"\\\\.\\"+state.port;
            const auto port=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(port==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not open "+Utf8(state.port)+" (Windows "+std::to_string(GetLastError())+").");
            struct Close{HANDLE h;~Close(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}}close{port};
            checkpoint(name+".get_comm_state");DCB line{sizeof(line)};if(!GetCommState(port,&line))throw std::runtime_error("GetCommState failed (Windows "+std::to_string(GetLastError())+").");
            line.BaudRate=115200;line.ByteSize=8;line.Parity=NOPARITY;line.StopBits=ONESTOPBIT;line.fDtrControl=DTR_CONTROL_ENABLE;line.fRtsControl=RTS_CONTROL_ENABLE;line.fOutxCtsFlow=false;line.fOutxDsrFlow=false;line.fOutX=line.fInX=false;
            checkpoint(name+".set_comm_state");if(!SetCommState(port,&line))throw std::runtime_error("SetCommState failed (Windows "+std::to_string(GetLastError())+").");
            COMMTIMEOUTS timeouts{50,0,2000,0,2000};if(!SetCommTimeouts(port,&timeouts))throw std::runtime_error("SetCommTimeouts failed (Windows "+std::to_string(GetLastError())+").");
            const Bytes request=protocol==Protocol::ModbusRtu?WithCrc({1,3,0,0,0,1}):Bytes{'*','O','P','C','?','\n'};
            const Bytes expected=protocol==Protocol::ModbusRtu?WithCrc({1,3,2,12,228}):Bytes{'1','\n'};
            checkpoint(name+".write_request");DWORD sent=0;if(!WriteFile(port,request.data(),static_cast<DWORD>(request.size()),&sent,nullptr)||sent!=request.size())throw std::runtime_error("COM request write failed (Windows "+std::to_string(GetLastError())+").");
            checkpoint(name+".read_response");Bytes received;const auto deadline=Now()+4000;
            while(received.size()<expected.size()&&Now()<deadline){std::array<std::uint8_t,256> bytes{};DWORD count=0;if(!ReadFile(port,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr))throw std::runtime_error("COM response read failed (Windows "+std::to_string(GetLastError())+").");received.insert(received.end(),bytes.begin(),bytes.begin()+count);}
            if(received!=expected)throw std::runtime_error("Protocol response mismatch. Expected "+Cdc::Hex(expected)+"; received "+Cdc::Hex(received)+".");
            checkpoint(name+".close_com");CloseHandle(close.h);close.h=INVALID_HANDLE_VALUE;
            checkpoint(name+".remove_device");session.Stop();
        }catch(const std::exception& e){detail=e.what();save(false);throw;}
    }success=true;phase="complete";detail="All virtual COM protocol roundtrips passed.";}catch(const std::exception& e){detail=e.what();}
    if(!save(true))return 2;return success?0:1;
}
}
