#include "Session.h"
#include <Windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace {
constexpr COLORREF Bg=RGB(14,20,31),Panel=RGB(25,35,51),White=RGB(239,245,255),Blue=RGB(46,112,246);
enum {Create=10,Stop,Setup,Set,Mode=30,Unit,Index,Value,StateView,Trace,StatusText,Counts};
HFONT font=nullptr,title=nullptr;HBRUSH background=nullptr,panel=nullptr;
bool preview=false;int autoProtocol=-1;unsigned autoUnit=1;
struct Window {HWND window=nullptr;Simulator::Session session;unsigned dpi=96;std::wstring notice;ULONGLONG noticeUntil=0;};
int P(Window& s,int n){return MulDiv(n,static_cast<int>(s.dpi),96);}
std::wstring Text(HWND control){const int n=GetWindowTextLengthW(control);std::wstring text(static_cast<std::size_t>(n)+1,0);GetWindowTextW(control,text.data(),n+1);text.resize(static_cast<std::size_t>(n));return text;}
void Changed(HWND control,const std::wstring& text){if(Text(control)!=text)SetWindowTextW(control,text.c_str());}
unsigned Number(HWND control,unsigned maximum){const auto text=Text(control);if(text.empty()||text.size()>5||!std::all_of(text.begin(),text.end(),[](wchar_t c){return c>='0'&&c<='9';}))throw std::invalid_argument("Enter an unsigned decimal number.");const auto n=std::stoul(text);if(n>maximum)throw std::out_of_range("Value exceeds the allowed range.");return static_cast<unsigned>(n);}
HWND Control(Window& s,int id,const wchar_t* cls,const wchar_t* label,DWORD style=0){const auto h=CreateWindowW(cls,label,WS_CHILD|WS_VISIBLE|style,0,0,10,10,s.window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;}
void Move(Window& s,int id,int x,int y,int w,int h){MoveWindow(GetDlgItem(s.window,id),P(s,x),P(s,y),P(s,w),P(s,h),TRUE);}
void Layout(Window& s){RECT r{};GetClientRect(s.window,&r);const int width=MulDiv(r.right,96,static_cast<int>(s.dpi)),height=MulDiv(r.bottom,96,static_cast<int>(s.dpi));
    Move(s,Create,28,105,180,38);Move(s,Stop,218,105,100,38);Move(s,Setup,width-172,105,144,38);
    Move(s,101,28,166,180,22);Move(s,Mode,28,195,280,220);Move(s,102,330,166,200,22);Move(s,Unit,330,195,90,32);
    Move(s,StatusText,28,250,width-56,62);Move(s,Counts,28,320,width-56,28);
    Move(s,103,28,373,330,24);Move(s,104,width/2+12,373,width/2-40,24);
    Move(s,StateView,28,410,width/2-44,height-550);Move(s,Trace,width/2+12,410,width/2-40,height-460);
    Move(s,105,28,height-121,110,24);Move(s,Index,28,height-88,82,32);Move(s,106,150,height-121,170,24);Move(s,Value,150,height-88,100,32);Move(s,Set,266,height-88,140,34);
}
bool Elevated(){HANDLE token=nullptr;TOKEN_ELEVATION e{};DWORD bytes=0;const bool result=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)&&GetTokenInformation(token,TokenElevation,&e,sizeof(e),&bytes)&&e.TokenIsElevated;if(token)CloseHandle(token);return result;}
void Start(Window& s){if(preview)return;const auto mode=SendDlgItemMessageW(s.window,Mode,CB_GETCURSEL,0,0);const auto unit=Number(GetDlgItem(s.window,Unit),247);if(!unit)throw std::invalid_argument("Unit address must be 1..247.");
    if(!Elevated()){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto args=L"--live "+std::to_wstring(mode)+L" "+std::to_wstring(unit);SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS;launch.hwnd=s.window;launch.lpVerb=L"runas";launch.lpFile=exe;launch.lpParameters=args.c_str();launch.nShow=SW_SHOWNORMAL;if(!ShellExecuteExW(&launch))throw std::runtime_error("Administrator access was not granted; no virtual instrument was created.");if(launch.hProcess)CloseHandle(launch.hProcess);PostMessageW(s.window,WM_CLOSE,0,0);return;}
    s.session.Start(mode==1?Simulator::Protocol::Scpi:Simulator::Protocol::ModbusRtu,unit);
}
void Refresh(Window& s){auto state=s.session.Read();if(preview){state.connected=state.ready=true;state.port=L"COM42 [sample]";state.message=L"OFFLINE PREVIEW. Sample instrument only; no driver or COM port is opened.";state.state.holding[0]=3300;state.state.requests=12;state.state.responses=12;state.log={L"RX  01 03 00 00 00 01 84 0A",L"TX  01 03 02 0C E4 BD 0F"};}
    Changed(GetDlgItem(s.window,StatusText),GetTickCount64()<s.noticeUntil?s.notice:state.message);
    Changed(GetDlgItem(s.window,Counts),std::to_wstring(state.state.requests)+L" requests   /   "+std::to_wstring(state.state.responses)+L" responses   /   "+std::to_wstring(state.state.rejected)+L" rejected   /   "+(state.port.empty()?L"No owned COM port":state.port));
    std::wostringstream values;for(unsigned n=0;n<128;++n){values<<std::setw(3)<<std::setfill(L'0')<<n<<L"    "<<std::setw(5)<<state.state.holding[n]<<L"\r\n";}
    Changed(GetDlgItem(s.window,StateView),values.str());std::wstring trace;for(const auto& line:state.log)trace+=line+L"\r\n";Changed(GetDlgItem(s.window,Trace),trace);
    EnableWindow(GetDlgItem(s.window,Set),state.connected&&!preview);EnableWindow(GetDlgItem(s.window,Create),!state.connected&&!preview);EnableWindow(GetDlgItem(s.window,Mode),!state.connected);EnableWindow(GetDlgItem(s.window,Unit),!state.connected);
}
LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){auto s=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(message==WM_NCCREATE){s=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}if(!s)return DefWindowProcW(hwnd,message,w,l);
    switch(message){
    case WM_CREATE:{s->dpi=GetDpiForWindow(hwnd);font=CreateFontW(-P(*s,16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title=CreateFontW(-P(*s,30),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
        for(const auto& b:{std::pair<int,const wchar_t*>{Create,L"Create instrument"},{Stop,L"Stop"},{Setup,L"Driver setup"},{Set,L"Set register"}})Control(*s,b.first,L"BUTTON",b.second,BS_OWNERDRAW|WS_TABSTOP);
        Control(*s,101,L"STATIC",L"PROTOCOL");Control(*s,Mode,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP);SendDlgItemMessageW(hwnd,Mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Modbus RTU - unit registers"));SendDlgItemMessageW(hwnd,Mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"SCPI - limited virtual instrument"));SendDlgItemMessageW(hwnd,Mode,CB_SETCURSEL,autoProtocol==1?1:0,0);
        Control(*s,102,L"STATIC",L"MODBUS UNIT  /  1..247");Control(*s,Unit,L"EDIT",std::to_wstring(autoUnit).c_str(),WS_BORDER|ES_NUMBER|WS_TABSTOP);
        Control(*s,StatusText,L"STATIC",L"Ready. No virtual device exists until you choose Create instrument.");Control(*s,Counts,L"STATIC",L"");
        Control(*s,103,L"STATIC",L"HOLDING REGISTERS  /  zero-based address");Control(*s,104,L"STATIC",L"REQUEST / RESPONSE HEX  /  latest 48 entries");
        Control(*s,StateView,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER);Control(*s,Trace,L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL|WS_BORDER);
        Control(*s,105,L"STATIC",L"INDEX 0..127");Control(*s,Index,L"EDIT",L"0",WS_BORDER|ES_NUMBER|WS_TABSTOP);Control(*s,106,L"STATIC",L"VALUE 0..65535");Control(*s,Value,L"EDIT",L"3300",WS_BORDER|ES_NUMBER|WS_TABSTOP);for(auto id:{Unit,Index,Value})SendDlgItemMessageW(hwnd,id,EM_SETLIMITTEXT,5,0);
        Layout(*s);Refresh(*s);SetTimer(hwnd,1,100,nullptr);if(autoProtocol>=0&&!preview)PostMessageW(hwnd,WM_COMMAND,Create,0);return 0;}
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={P(*s,960),P(*s,760)};return 0;
    case WM_SIZE:Layout(*s);return 0;
    case WM_DPICHANGED:{s->dpi=HIWORD(w);const auto oldFont=font,oldTitle=title;font=CreateFontW(-P(*s,16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title=CreateFontW(-P(*s,30),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");for(auto child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);DeleteObject(oldFont);DeleteObject(oldTitle);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout(*s);return 0;}
    case WM_TIMER:Refresh(*s);return 0;
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED)try{switch(LOWORD(w)){case Create:Start(*s);break;case Stop:s->session.Stop();Refresh(*s);break;case Set:s->session.SetRegister(Number(GetDlgItem(hwnd,Index),127),static_cast<std::uint16_t>(Number(GetDlgItem(hwnd,Value),65535)));break;case Setup:{wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto setup=std::filesystem::path(exe).parent_path()/L"Dependencies"/L"VdeviceSetup.exe";if(!std::filesystem::exists(setup))throw std::runtime_error("Extract the complete release ZIP; Dependencies/VdeviceSetup.exe is missing.");if(reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"open",setup.c_str(),nullptr,setup.parent_path().c_str(),SW_SHOWNORMAL))<=32)throw std::runtime_error("Could not open Vdevice setup.");break;}default:break;}}catch(const std::exception& e){const std::string text=e.what();s->notice=std::wstring(text.begin(),text.end());s->noticeUntil=GetTickCount64()+5000;Changed(GetDlgItem(hwnd,StatusText),s->notice);}return 0;
    case WM_SETCURSOR:if(GetClassLongPtrW(reinterpret_cast<HWND>(w),GCW_ATOM)==GetClassLongPtrW(GetDlgItem(hwnd,Create),GCW_ATOM)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
    case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(w),White);SetBkColor(reinterpret_cast<HDC>(w),message==WM_CTLCOLOREDIT?Panel:Bg);return reinterpret_cast<LRESULT>(message==WM_CTLCOLOREDIT?panel:background);
    case WM_DRAWITEM:{const auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);const auto brush=CreateSolidBrush(d->CtlID==Create||d->CtlID==Set?Blue:Panel);FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,White);SelectObject(d->hDC,font);wchar_t label[80]{};GetWindowTextW(d->hwndItem,label,80);DrawTextW(d->hDC,label,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);return TRUE;}
    case WM_PAINT:{PAINTSTRUCT ps{};const auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);FillRect(dc,&r,background);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,White);SelectObject(dc,title);RECT box{P(*s,28),P(*s,22),r.right,P(*s,65)};DrawTextW(dc,L"Serial Device Simulator",-1,&box,DT_LEFT);SelectObject(dc,font);box={P(*s,28),P(*s,69),r.right,P(*s,96)};DrawTextW(dc,L"A virtual instrument on a real Windows COM port  /  Modbus RTU + limited SCPI",-1,&box,DT_LEFT);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_DESTROY:KillTimer(hwnd,1);s->session.Stop();PostQuitMessage(0);return 0;
    default:break;}return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring screenshot;bool self=false;
    try{for(int i=1;i<count;++i){const std::wstring a=args[i];if(a==L"--preview")preview=true;else if(a==L"--self-test"){preview=true;self=true;}else if(a==L"--screenshot"&&i+1<count){preview=true;self=true;screenshot=args[++i];}else if(a==L"--live"&&i+2<count){autoProtocol=std::stoi(args[++i]);autoUnit=static_cast<unsigned>(std::stoul(args[++i]));if(autoProtocol<0||autoProtocol>1||autoUnit<1||autoUnit>247)throw std::invalid_argument("Invalid live mode.");}else throw std::invalid_argument("Unknown argument.");}}catch(...){LocalFree(args);return 2;}LocalFree(args);
    background=CreateSolidBrush(Bg);panel=CreateSolidBrush(Panel);WNDCLASSW cls{};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"TitanSerialSimulator";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&cls))return 1;Window s;s.dpi=GetDpiForSystem();const auto hwnd=CreateWindowW(cls.lpszClassName,L"Serial Device Simulator | Titanfyre",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,P(s,1100),P(s,880),nullptr,nullptr,instance,&s);if(!hwnd)return 1;ShowWindow(hwnd,show);UpdateWindow(hwnd);
    if(self){bool okay=GetDlgItem(hwnd,Create)&&GetDlgItem(hwnd,StateView)&&!s.session.Read().connected;
        if(!screenshot.empty()){ShowWindow(hwnd,SW_SHOWNOACTIVATE);RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)okay=false;else{RECT r{};GetWindowRect(hwnd,&r);const auto screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);const auto image=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);const auto old=SelectObject(memory,image);okay=okay&&PrintWindow(hwnd,memory,PW_RENDERFULLCONTENT);SelectObject(memory,old);{Gdiplus::Bitmap bitmap(image,nullptr);const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};okay=okay&&bitmap.Save(screenshot.c_str(),&png,nullptr)==Gdiplus::Ok;}DeleteObject(image);DeleteDC(memory);ReleaseDC(nullptr,screen);Gdiplus::GdiplusShutdown(token);}}
        DestroyWindow(hwnd);DeleteObject(font);DeleteObject(title);DeleteObject(background);DeleteObject(panel);return okay?0:1;}
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}DeleteObject(font);DeleteObject(title);DeleteObject(background);DeleteObject(panel);return static_cast<int>(message.wParam);
}
