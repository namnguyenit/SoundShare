#include "soundshare/desktop_client.hpp"
#include "soundshare/platform.hpp"
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sd=soundshare::desktop;
using soundshare::narrow;
using soundshare::widen;
namespace {
constexpr wchar_t window_class[]=L"SoundShare.Desktop.Main";
constexpr UINT tray_message=WM_APP+42;
constexpr COLORREF background=RGB(243,246,250),white=RGB(255,255,255),ink=RGB(21,35,53),muted=RGB(100,116,139);
constexpr COLORREF border=RGB(222,230,238),accent=RGB(12,122,120),sidebar=RGB(19,35,51),nav_active=RGB(33,62,77);
constexpr COLORREF danger=RGB(171,62,62);
enum Id {
    nav_home=100,nav_diagnostics,nav_guide,source_mode,capture_device,bridge_device,
    output_list,source_list,refresh_devices,open_receiver,close_receiver,auto_reconnect,
    output_gain,output_mute,output_delay,only_output,master_gain,quality_mode,start_stop,
    sound_settings,bluetooth_settings,freeze_diagnostics,export_diagnostics,save_profile,load_profile,
    hide_window,exit_app,tray_open,tray_stop,tray_exit
};
struct Widget {HWND handle{};int page{-1};};
std::wstring control_text(HWND control) {
    const int length=GetWindowTextLengthW(control);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
    GetWindowTextW(control,value.data(),length+1);value.resize(static_cast<std::size_t>(length));return value;
}
std::wstring state_label(const std::string& state) {
    if(state=="running") return L"Đang cấp âm thanh";
    if(state=="opening") return L"Đang mở…";
    if(state=="retry_wait") return L"Đang kết nối lại";
    if(state=="failed") return L"Cần kiểm tra";
    if(state=="opened") return L"Đã mở nhận";
    if(state=="closed") return L"Kết nối đã đóng";
    if(state=="available") return L"Sẵn sàng";
    return L"Chưa kết nối";
}
std::string choose_file(HWND owner,bool save,const wchar_t* title) {
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    if(save) soundshare::check_hr(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"save dialog");
    else soundshare::check_hr(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"open dialog");
    DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|(save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    const COMDLG_FILTERSPEC filter[]={{L"Cấu hình JSON",L"*.json"}};
    dialog->SetFileTypes(1,filter);dialog->SetDefaultExtension(L"json");dialog->SetTitle(title);
    // Use the application's profile folder instead of the launcher's working directory.
    const auto folder=std::filesystem::path(widen(sd::default_profile_path())).parent_path();
    Microsoft::WRL::ComPtr<IShellItem> default_folder;
    if(SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(),nullptr,IID_PPV_ARGS(&default_folder))))
        dialog->SetDefaultFolder(default_folder.Get());
    if(save) dialog->SetFileName(L"soundshare.json");
    const auto result=dialog->Show(owner);if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    soundshare::check_hr(result,"file dialog");Microsoft::WRL::ComPtr<IShellItem> item;
    soundshare::check_hr(dialog->GetResult(&item),"selected file");PWSTR path=nullptr;
    soundshare::check_hr(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"selected file path");
    auto text=narrow(path);CoTaskMemFree(path);return text;
}
class Window {
public:
    HWND hwnd{};
    ~Window() {if(client_) client_->stop();destroy_fonts();if(white_brush_)DeleteObject(white_brush_);if(bg_brush_)DeleteObject(bg_brush_);}
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
        auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->hwnd=hwnd;
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self) return DefWindowProcW(hwnd,message,wp,lp);
        try {return self->message(message,wp,lp);}
        catch(const std::exception& error) {self->notice(widen(error.what()),true);return message==WM_CREATE ? -1 : 0;}
    }
private:
    UINT dpi_{96},taskbar_created_{RegisterWindowMessageW(L"TaskbarCreated")};
    int page_{0};bool updating_{false},online_{false},closing_{false},pending_session_{false},pending_profile_{false},configure_timer_{false},pending_master_{false};
    unsigned ticks_{0};
    sd::Model model_;
    std::unique_ptr<sd::Client> client_;
    std::string profile_path_,selected_output_,selected_source_,last_error_;
    sd::Json last_inventory_=sd::Json::object();
    ULONGLONG banner_until_{0};
    std::vector<std::string> capture_options_,bridge_options_,output_ids_,source_ids_;
    std::set<std::string> pending_outputs_,busy_sources_;
    std::vector<Widget> widgets_;
    HFONT body_{},semibold_{},title_font_{},small_{},mono_{};
    HBRUSH white_brush_{CreateSolidBrush(white)},bg_brush_{CreateSolidBrush(background)};
    RECT capture_card_{},receiver_card_{},outputs_card_{},footer_card_{},page_card_{};
    HWND title_{},subtitle_{},banner_{},backend_label_{},capture_label_{},bridge_label_{},source_hint_{},
        selected_label_{},gain_label_{},delay_label_{},master_label_{},quality_label_{},diagnostics_summary_{},
        diagnostics_text_{},guide_text_{},receiver_hint_{};
    NOTIFYICONDATAW tray_{};bool tray_added_{false};
    int px(int value) const {return MulDiv(value,static_cast<int>(dpi_),96);}
    HWND get(int id) const {return GetDlgItem(hwnd,id);}
    HWND create(const wchar_t* klass,const wchar_t* text,int id,DWORD style,int page=0,DWORD ex=0) {
        HWND control=CreateWindowExW(ex,klass,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!control) soundshare::check_hr(HRESULT_FROM_WIN32(GetLastError()),"create desktop control");
        widgets_.push_back({control,page});SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(body_),FALSE);return control;
    }
    HWND label(const wchar_t* text,int page=0) {return create(L"STATIC",text,0,SS_LEFT|SS_NOPREFIX,page);}
    void place(HWND control,int x,int y,int width,int height) {
        MoveWindow(control,px(x),px(y),std::max(0,px(width)),std::max(0,px(height)),TRUE);
    }
    void font(HWND control,HFONT value) {SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(value),TRUE);}
    void notice(const std::wstring& value,bool error=false) {
        banner_error_=error;banner_until_=GetTickCount64()+(error ? 10000 : 7000);
        SetWindowTextW(banner_,value.c_str());InvalidateRect(banner_,nullptr,TRUE);
    }
    void summary() {
        if(GetTickCount64()<banner_until_ || closing_)return;
        banner_error_=false;std::wstring text;
        if(!online_)text=L"Đang chờ bộ xử lý. Bạn vẫn có thể xem và chỉnh lựa chọn.";
        else if(model_.active){
            unsigned count=0;for(const auto& output:model_.outputs)if(!stream_for(output.id).empty())++count;
            text=model_.all_running ? L"Đang chia sẻ tới "+std::to_wstring(count)+L" đầu ra. Âm lượng chung: "+std::to_wstring(std::lround(model_.master_gain*100))+L"%." : L"Đang chuẩn bị đầu ra. Nếu chưa có tiếng, xem Chẩn đoán.";
        }else text=L"Đã chọn "+std::to_wstring(model_.selected_count())+L" thiết bị. Nhấn Bắt đầu chia sẻ khi bạn sẵn sàng.";
        SetWindowTextW(banner_,text.c_str());InvalidateRect(banner_,nullptr,TRUE);
    }
    bool banner_error_{false};
    void command(const std::string& method,sd::Json params=sd::Json::object(),const std::string& key={}) {
        client_->submit({sd::JobKind::command,method,key,{},std::move(params)});
    }
    void save_later() {SetTimer(hwnd,2,450,nullptr);}
    void send_profile(const std::string& path) {client_->submit({sd::JobKind::save,{},"save:"+path,path,model_.profile()});}
    void configure_later(const std::string& id={}) {
        if(!id.empty()) pending_outputs_.insert(id);
        if(!configure_timer_) {SetTimer(hwnd,3,120,nullptr);configure_timer_=true;}
    }
    void destroy_fonts() {
        for(HFONT value:{body_,semibold_,title_font_,small_,mono_}) if(value)DeleteObject(value);
        body_=semibold_=title_font_=small_=mono_=nullptr;
    }
    void make_fonts() {
        const auto make=[&](int size,int weight,const wchar_t* family) {
            return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,family);
        };
        const auto old=std::vector<HFONT>{body_,semibold_,title_font_,small_,mono_};
        body_=make(14,FW_NORMAL,L"Segoe UI");semibold_=make(15,FW_SEMIBOLD,L"Segoe UI");title_font_=make(26,FW_SEMIBOLD,L"Segoe UI");
        small_=make(12,FW_NORMAL,L"Segoe UI");mono_=make(12,FW_NORMAL,L"Consolas");
        for(const auto& widget:widgets_) font(widget.handle,body_);
        if(title_)font(title_,title_font_);
        if(diagnostics_text_)font(diagnostics_text_,mono_);
        for(const auto& widget:widgets_) {
            const int id=GetDlgCtrlID(widget.handle);if(id>=nav_home && id<=nav_guide)font(widget.handle,semibold_);
        }
        for(auto value:old) if(value)DeleteObject(value);
    }
    void initialize() {
        dpi_=GetDpiForWindow(hwnd);make_fonts();profile_path_=sd::default_profile_path();
        try {if(std::filesystem::exists(std::filesystem::path(widen(profile_path_)))) model_.load_profile(sd::read_json_file(profile_path_));}
        catch(const std::exception& error) {last_error_=error.what();}
        create(L"BUTTON",L"Chia sẻ",nav_home,BS_OWNERDRAW|WS_TABSTOP,-1);
        create(L"BUTTON",L"Chẩn đoán",nav_diagnostics,BS_OWNERDRAW|WS_TABSTOP,-1);
        create(L"BUTTON",L"Hướng dẫn",nav_guide,BS_OWNERDRAW|WS_TABSTOP,-1);
        create(L"BUTTON",L"Thu về khay",hide_window,BS_OWNERDRAW|WS_TABSTOP,-1);
        title_=label(L"Chia sẻ âm thanh",-1);font(title_,title_font_);
        subtitle_=label(L"Nhận từ điện thoại, TV box và nghe trên những thiết bị bạn chọn.",-1);
        banner_=label(L"Đang kết nối bộ xử lý…",-1);backend_label_=label(L"Đang khởi động",-1);
        create(L"BUTTON",L"Làm mới",refresh_devices,BS_OWNERDRAW|WS_TABSTOP,-1);
        auto section=[&](const wchar_t* text) {auto h=label(text);font(h,semibold_);return h;};
        capture_title_=section(L"Nguồn âm thanh");
        create(WC_COMBOBOXW,L"Chọn loại nguồn",source_mode,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        SendMessageW(get(source_mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Âm thanh đang phát trên PC"));
        SendMessageW(get(source_mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Đầu vào / cáp âm thanh ảo"));
        capture_label_=label(L"Lấy âm thanh từ");
        create(WC_COMBOBOXW,L"Chọn nguồn âm thanh",capture_device,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        bridge_label_=label(L"Đầu phát tương ứng của cáp ảo");
        create(WC_COMBOBOXW,L"Đầu phát của cáp ảo",bridge_device,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        source_hint_=label(L"Để căn độ trễ cho mọi loa, dùng một cáp âm thanh ảo làm nguồn chung.");
        create(L"BUTTON",L"Cài đặt âm thanh",sound_settings,BS_OWNERDRAW|WS_TABSTOP);
        receiver_title_=section(L"Điện thoại & TV box");
        create(L"BUTTON",L"Bluetooth",bluetooth_settings,BS_OWNERDRAW|WS_TABSTOP);
        create(WC_LISTVIEWW,L"Thiết bị nguồn Bluetooth",source_list,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,0,WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(get(source_list),LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        add_column(get(source_list),0,L"Thiết bị",180);add_column(get(source_list),1,L"Trạng thái",110);
        create(L"BUTTON",L"Nhận âm thanh",open_receiver,BS_OWNERDRAW|WS_TABSTOP);
        create(L"BUTTON",L"Dừng nhận",close_receiver,BS_OWNERDRAW|WS_TABSTOP);
        create(L"BUTTON",L"Tự kết nối lại trong phiên",auto_reconnect,BS_AUTOCHECKBOX|WS_TABSTOP);
        receiver_hint_=label(L"Bắt đầu chia sẻ trước khi mở nhận.");font(receiver_hint_,small_);
        outputs_title_=section(L"Thiết bị phát");outputs_hint_=label(L"Đánh dấu các tai nghe hoặc loa bạn muốn nghe.");
        create(WC_LISTVIEWW,L"Chọn thiết bị phát",output_list,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,0,WS_EX_CLIENTEDGE);
        ListView_SetExtendedListViewStyle(get(output_list),LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        add_column(get(output_list),0,L"Thiết bị",280);add_column(get(output_list),1,L"Trạng thái",160);add_column(get(output_list),2,L"Tín hiệu",70);
        selected_label_=label(L"Chọn một dòng để chỉnh riêng thiết bị đó.");font(selected_label_,semibold_);
        gain_label_=label(L"Âm lượng: 75%");
        create(TRACKBAR_CLASSW,L"Âm lượng thiết bị phát",output_gain,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP);
        SendMessageW(get(output_gain),TBM_SETRANGE,TRUE,MAKELONG(0,100));
        create(L"BUTTON",L"Tắt tiếng",output_mute,BS_AUTOCHECKBOX|WS_TABSTOP);
        delay_label_=label(L"Bù độ trễ (ms)");
        create(L"EDIT",L"0",output_delay,ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,0,WS_EX_CLIENTEDGE);
        SendMessageW(get(output_delay),EM_SETLIMITTEXT,4,0);
        create(L"BUTTON",L"Chỉ phát thiết bị này",only_output,BS_OWNERDRAW|WS_TABSTOP);
        output_note_=label(L"Đổi độ trễ sẽ nạp lại bộ đệm của thiết bị đó.");font(output_note_,small_);
        master_label_=label(L"Âm lượng chung: 50%",-1);
        create(TRACKBAR_CLASSW,L"Âm lượng chung",master_gain,TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,-1);
        SendMessageW(get(master_gain),TBM_SETRANGE,TRUE,MAKELONG(0,100));
        quality_label_=label(L"Chế độ nghe",-1);
        create(WC_COMBOBOXW,L"Chế độ nghe",quality_mode,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,-1);
        for(const auto* text:{L"Cân bằng",L"Ưu tiên ổn định",L"Ưu tiên phản hồi"}) SendMessageW(get(quality_mode),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        create(L"BUTTON",L"Bắt đầu chia sẻ",start_stop,BS_OWNERDRAW|WS_TABSTOP,-1);
        diagnostics_summary_=label(L"Chưa có phiên chia sẻ.",1);
        create(L"BUTTON",L"Đóng băng",freeze_diagnostics,BS_AUTOCHECKBOX|WS_TABSTOP,1);
        create(L"BUTTON",L"Xuất chẩn đoán…",export_diagnostics,BS_OWNERDRAW|WS_TABSTOP,1);
        diagnostics_text_=create(L"EDIT",L"Đang chờ bộ xử lý…",0,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,1,WS_EX_CLIENTEDGE);font(diagnostics_text_,mono_);
        guide_text_=create(L"EDIT",guide_text(),0,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,2);font(guide_text_,body_);
        create(L"BUTTON",L"Lưu cấu hình…",save_profile,BS_OWNERDRAW|WS_TABSTOP,2);
        create(L"BUTTON",L"Mở cấu hình…",load_profile,BS_OWNERDRAW|WS_TABSTOP,2);
        client_=std::make_unique<sd::Client>(hwnd);client_->submit({sd::JobKind::bootstrap,{},"refresh"});
        add_tray();SetTimer(hwnd,1,1000,nullptr);sync_controls();layout();
    }
    HWND capture_title_{},receiver_title_{},outputs_title_{},outputs_hint_{},output_note_{};
    static const wchar_t* guide_text() {
        return L"BẮT ĐẦU NGHE TRÊN NHIỀU THIẾT BỊ\r\n\r\n"
            L"1. Kết nối tai nghe hoặc loa trong Windows. Thiết bị phải sẵn sàng để xuất hiện trong danh sách phát.\r\n\r\n"
            L"2. Ở Nguồn âm thanh, chọn âm thanh PC hoặc đầu vào của cáp âm thanh ảo. Nếu dùng cáp ảo, chọn thêm đầu phát tương ứng để tránh đưa âm thanh quay lại chính nguồn.\r\n\r\n"
            L"3. Đánh dấu các thiết bị phát. Chọn một dòng để chỉnh âm lượng, tắt tiếng hoặc bù độ trễ riêng. Sau đó nhấn Bắt đầu chia sẻ.\r\n\r\n"
            L"NHẬN TỪ ĐIỆN THOẠI HOẶC TV BOX\r\n\r\n"
            L"Ghép đôi thiết bị với PC trong cài đặt Bluetooth. Trên thiết bị nguồn, chọn PC làm nơi phát âm thanh media. Khi phiên chia sẻ đã bắt đầu, chọn nguồn trong SoundShare và nhấn Nhận âm thanh.\r\n\r\n"
            L"Nếu nhận qua cáp ảo, hãy chọn đầu phát của cáp trong cài đặt âm thanh Windows và kiểm tra âm thanh có đi đúng đường. Ứng dụng không tự đổi đầu ra mặc định hoặc cài driver.\r\n\r\n"
            L"Đã mở nhận chỉ xác nhận kết nối, không chứng minh thiết bị đang phát nhạc. Khả năng điện thoại và TV box cùng truyền, rồi phát tới hai tai nghe Bluetooth, cần được thử trên đúng thiết bị của bạn.\r\n\r\n"
            L"ĐỒNG BỘ VÀ ĐỘ MƯỢT\r\n\r\n"
            L"Bắt đầu với Cân bằng. Nếu đường truyền hoặc máy bận, thử Ưu tiên ổn định. Đổi chế độ cần dừng phiên. Bù độ trễ làm chậm thêm đầu ra đang phát sớm hơn; không làm loa Bluetooth phát sớm lên.\r\n\r\n"
            L"THU VỀ KHAY VÀ KẾT THÚC\r\n\r\n"
            L"Đóng cửa sổ khi đang chia sẻ sẽ thu về khay hệ thống. Dùng menu khay để mở lại hoặc Thoát và dừng âm thanh. Nếu đang dùng cáp ảo làm mặc định, chọn lại loa vật lý trong Windows khi kết thúc.\r\n\r\n"
            L"Lựa chọn thiết bị và âm lượng được lưu trên máy. Mở ứng dụng không tự nhận âm thanh hoặc tự phát. Chẩn đoán có số liệu phần mềm; đó không phải số gói Bluetooth bị mất hay phép đo âm thanh thực sự tới tai nghe.";
    }
    void add_column(HWND list,int index,const wchar_t* title,int width) {
        LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<LPWSTR>(title);column.cx=px(width);ListView_InsertColumn(list,index,&column);
    }
    void layout() {
        RECT client{};GetClientRect(hwnd,&client);const int width=MulDiv(client.right,96,dpi_),height=MulDiv(client.bottom,96,dpi_);
        const int left=204,right=width-24,content=right-left,footer=height-94;
        place(get(nav_home),16,130,156,44);place(get(nav_diagnostics),16,184,156,44);place(get(nav_guide),16,238,156,44);
        place(get(hide_window),16,height-70,156,36);
        place(title_,left,23,content-180,36);place(subtitle_,left,63,content-100,22);
        place(backend_label_,right-174,28,174,22);place(get(refresh_devices),right-96,66,96,30);
        place(banner_,left,91,content-110,22);
        capture_card_={left,122,left+314,372};receiver_card_={left,384,left+314,footer-14};outputs_card_={left+330,122,right,footer-14};
        footer_card_={left,footer,right,height-14};page_card_={left,122,right,footer-14};
        place(capture_title_,left+16,138,280,22);place(get(source_mode),left+16,170,282,240);
        place(capture_label_,left+16,210,280,18);place(get(capture_device),left+16,232,282,260);
        place(bridge_label_,left+16,272,280,18);place(get(bridge_device),left+16,294,282,240);
        place(source_hint_,left+16,272,280,48);place(get(sound_settings),left+16,332,170,28);
        const int ry=receiver_card_.top,rh=receiver_card_.bottom-ry;
        place(receiver_title_,left+16,ry+14,192,24);place(get(bluetooth_settings),left+218,ry+11,80,28);
        place(get(source_list),left+16,ry+48,282,std::max(60,rh-134));
        place(get(open_receiver),left+16,receiver_card_.bottom-76,148,28);place(get(close_receiver),left+174,receiver_card_.bottom-76,124,28);
        place(get(auto_reconnect),left+16,receiver_card_.bottom-39,282,22);place(receiver_hint_,left+16,receiver_card_.bottom-18,282,16);
        ListView_SetColumnWidth(get(source_list),0,px(167));ListView_SetColumnWidth(get(source_list),1,px(110));
        const int ox=outputs_card_.left,ow=outputs_card_.right-ox,ob=outputs_card_.bottom;
        place(outputs_title_,ox+18,138,ow-36,24);place(outputs_hint_,ox+18,169,ow-36,20);
        place(get(output_list),ox+18,200,ow-36,std::max(90,ob-200-186));
        ListView_SetColumnWidth(get(output_list),0,px(std::max(140,ow-278)));
        ListView_SetColumnWidth(get(output_list),1,px(154));ListView_SetColumnWidth(get(output_list),2,px(72));
        place(selected_label_,ox+18,ob-174,ow-36,22);place(gain_label_,ox+18,ob-142,122,20);
        place(get(output_gain),ox+148,ob-145,std::max(80,ow-286),30);place(get(output_mute),outputs_card_.right-115,ob-141,97,24);
        place(delay_label_,ox+18,ob-102,139,20);place(get(output_delay),ox+160,ob-107,72,28);
        place(get(only_output),ox+18,ob-64,194,30);place(output_note_,ox+18,ob-24,ow-36,16);
        place(master_label_,left+16,footer+12,198,20);place(get(master_gain),left+14,footer+37,204,28);
        place(quality_label_,left+242,footer+12,185,18);place(get(quality_mode),left+242,footer+36,185,200);
        place(get(start_stop),right-188,footer+23,170,38);
        place(diagnostics_summary_,left+18,140,content-330,40);place(get(freeze_diagnostics),right-292,140,112,28);
        place(get(export_diagnostics),right-166,136,148,32);place(diagnostics_text_,left+18,193,content-36,std::max(100,footer-226));
        place(guide_text_,left+22,140,content-44,std::max(100,footer-214));
        place(get(save_profile),left+22,footer-60,152,32);place(get(load_profile),left+186,footer-60,152,32);
        update_visibility();InvalidateRect(hwnd,nullptr,TRUE);
    }
    void update_visibility() {
        for(const auto& widget:widgets_) ShowWindow(widget.handle,widget.page<0 || widget.page==page_ ? SW_SHOW : SW_HIDE);
        if(page_==0) {
            ShowWindow(bridge_label_,model_.mode=="capture" ? SW_SHOW : SW_HIDE);
            ShowWindow(get(bridge_device),model_.mode=="capture" ? SW_SHOW : SW_HIDE);
            ShowWindow(source_hint_,model_.mode=="loopback" ? SW_SHOW : SW_HIDE);
        }
        SetWindowTextW(title_,page_==0 ? L"Chia sẻ âm thanh" : page_==1 ? L"Chẩn đoán phiên chia sẻ" : L"Bắt đầu với SoundShare");
        SetWindowTextW(subtitle_,page_==0 ? L"Nhận từ điện thoại, TV box và nghe trên những thiết bị bạn chọn." : page_==1 ? L"Số liệu xử lý trên máy; chất lượng nghe cần được kiểm tra tại thiết bị." : L"Kết nối nguồn, chọn nơi nghe và lưu cấu hình của bạn.");
        for(int id=nav_home;id<=nav_guide;++id) InvalidateRect(get(id),nullptr,TRUE);
    }
    void set_combo(HWND combo,const std::vector<sd::Endpoint>& options,const std::string& selected,std::vector<std::string>& ids,bool optional=false) {
        SendMessageW(combo,CB_RESETCONTENT,0,0);ids.clear();int current=-1;
        if(optional) {SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Chưa chọn / không dùng cáp ảo"));ids.push_back("");current=0;}
        for(const auto& option:options) {
            auto text=widen(option.name);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            if(option.id==selected)current=static_cast<int>(ids.size());ids.push_back(option.id);
        }
        if(!selected.empty() && std::find(ids.begin(),ids.end(),selected)==ids.end()) {
            auto text=L"Chưa kết nối: "+widen(model_.name_of(selected));SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            current=static_cast<int>(ids.size());ids.push_back(selected);
        }
        SendMessageW(combo,CB_SETCURSEL,current,0);
    }
    void sync_controls() {
        updating_=true;
        SendMessageW(get(source_mode),CB_SETCURSEL,model_.mode=="loopback" ? 0 : 1,0);
        set_combo(get(capture_device),model_.capture_options(),model_.capture_id,capture_options_);
        set_combo(get(bridge_device),model_.render_options(),model_.bridge_render_id,bridge_options_,true);
        SendMessageW(get(master_gain),TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround(model_.master_gain*100)));
        SendMessageW(get(quality_mode),CB_SETCURSEL,model_.buffer_ms>=200 ? 1 : model_.buffer_ms<=80 ? 2 : 0,0);
        SendMessageW(get(auto_reconnect),BM_SETCHECK,model_.auto_reconnect ? BST_CHECKED : BST_UNCHECKED,0);
        SetWindowTextW(master_label_,(L"Âm lượng chung: "+std::to_wstring(std::lround(model_.master_gain*100))+L"%").c_str());
        rebuild_outputs();rebuild_sources();update_selected_output();updating_=false;update_enabled();update_visibility();
    }
    int insert_row(HWND list,int index,const std::wstring& name) {
        LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=const_cast<LPWSTR>(name.c_str());return ListView_InsertItem(list,&item);
    }
    void set_cell(HWND list,int row,int column,const std::wstring& value) {ListView_SetItemText(list,row,column,const_cast<LPWSTR>(value.c_str()));}
    sd::Json stream_for(const std::string& id) const {
        if(model_.last_status.contains("engine")) for(const auto& item:model_.last_status.at("engine").at("outputs")) if(item.at("id")==id) return item;
        return sd::Json::object();
    }
    void rebuild_outputs() {
        if(std::find(output_ids_.begin(),output_ids_.end(),selected_output_)!=output_ids_.end() &&
           std::none_of(model_.outputs.begin(),model_.outputs.end(),[&](const auto& output){return output.id==selected_output_;}))selected_output_.clear();
        SendMessageW(get(output_list),WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(get(output_list));output_ids_.clear();
        for(const auto& output:model_.outputs) {
            int row=insert_row(get(output_list),static_cast<int>(output_ids_.size()),widen(output.name));output_ids_.push_back(output.id);
            ListView_SetCheckState(get(output_list),row,output.selected);
            if(output.id==selected_output_)ListView_SetItemState(get(output_list),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        }
        if(selected_output_.empty() && !output_ids_.empty()) {selected_output_=output_ids_.front();ListView_SetItemState(get(output_list),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
        SendMessageW(get(output_list),WM_SETREDRAW,TRUE,0);InvalidateRect(get(output_list),nullptr,TRUE);update_output_states();
    }
    void update_output_states() {
        for(std::size_t i=0;i<output_ids_.size();++i) {
            const auto& id=output_ids_[i];const auto stream=stream_for(id);const auto& output=model_.output(id);
            std::wstring state;
            if(model_.blocked(id)) state=L"Đang làm nguồn";
            else if(!model_.available(id))state=L"Chưa kết nối";
            else if(!stream.empty() && model_.active)state=state_label(stream.value("state",""));
            else state=output.selected && model_.active ? L"Chưa tham gia" : L"Sẵn sàng";
            set_cell(get(output_list),static_cast<int>(i),1,state);
            const auto peak=stream.value("peak",0.0);set_cell(get(output_list),static_cast<int>(i),2,
                output.muted ? L"Tắt tiếng" : !stream.empty() && model_.active ? std::to_wstring(static_cast<int>(std::clamp(peak,0.0,1.0)*100))+L"%" : L"—");
        }
    }
    void rebuild_sources() {
        std::vector<std::string> next_ids;for(const auto& source:model_.sources)next_ids.push_back(source.id);
        if(next_ids==source_ids_){
            for(std::size_t i=0;i<model_.sources.size();++i){set_cell(get(source_list),static_cast<int>(i),0,widen(model_.sources[i].name));set_cell(get(source_list),static_cast<int>(i),1,state_label(model_.sources[i].state));}
            return;
        }
        const bool previous=updating_;updating_=true;SendMessageW(get(source_list),WM_SETREDRAW,FALSE,0);
        ListView_DeleteAllItems(get(source_list));source_ids_.clear();
        for(const auto& source:model_.sources) {
            const int row=insert_row(get(source_list),static_cast<int>(source_ids_.size()),widen(source.name));source_ids_.push_back(source.id);
            set_cell(get(source_list),row,1,state_label(source.state));
            if(source.id==selected_source_)ListView_SetItemState(get(source_list),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        }
        if(selected_source_.empty() && !source_ids_.empty()){selected_source_=source_ids_.front();ListView_SetItemState(get(source_list),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
        SendMessageW(get(source_list),WM_SETREDRAW,TRUE,0);InvalidateRect(get(source_list),nullptr,TRUE);updating_=previous;
    }
    void update_selected_output() {
        const auto found=std::find(output_ids_.begin(),output_ids_.end(),selected_output_);
        if(found==output_ids_.end()) {selected_output_.clear();SetWindowTextW(selected_label_,L"Chọn một dòng để chỉnh riêng thiết bị đó.");return;}
        const auto& output=model_.output(selected_output_);const bool previous=updating_;updating_=true;
        SetWindowTextW(selected_label_,widen(output.name).c_str());
        SendMessageW(get(output_gain),TBM_SETPOS,TRUE,static_cast<LPARAM>(std::lround(output.gain*100)));
        SetWindowTextW(gain_label_,(L"Âm lượng: "+std::to_wstring(std::lround(output.gain*100))+L"%").c_str());
        SendMessageW(get(output_mute),BM_SETCHECK,output.muted ? BST_CHECKED : BST_UNCHECKED,0);
        SetWindowTextW(get(output_delay),std::to_wstring(output.delay_ms).c_str());updating_=previous;
    }
    void update_enabled() {
        const bool stable=!closing_ && !pending_session_ && !pending_profile_;
        const bool editable=stable && !model_.active;
        for(int id:{source_mode,capture_device,bridge_device,quality_mode,load_profile})EnableWindow(get(id),editable);
        const bool output=stable && !selected_output_.empty();
        for(int id:{output_gain,output_mute,output_delay,only_output})EnableWindow(get(id),output);
        EnableWindow(get(start_stop),online_ && stable);EnableWindow(get(master_gain),stable);
        EnableWindow(get(output_list),stable);
        bool can_open=false,can_close=false;
        for(const auto& source:model_.sources) if(source.id==selected_source_) {
            can_open=source.available && source.state!="opened" && source.state!="opening";
            can_close=source.state=="opened" || source.state=="closed" || source.state=="opening" || source.state=="unavailable";
        }
        const bool receiver=online_ && !closing_ && !busy_sources_.contains(selected_source_);
        EnableWindow(get(open_receiver),receiver && model_.active && can_open);EnableWindow(get(close_receiver),receiver && can_close);
        EnableWindow(get(export_diagnostics),online_ && !closing_);
        SetWindowTextW(receiver_hint_,model_.active ? L"Đã mở nhận chưa có nghĩa là đang phát." : L"Bắt đầu chia sẻ trước khi mở nhận.");
        SetWindowTextW(get(start_stop),closing_ ? L"Đang kết thúc…" : pending_session_ ? L"Đang xử lý…" : model_.active ? L"Dừng chia sẻ" : L"Bắt đầu chia sẻ");
        SetWindowTextW(backend_label_,online_ ? L"Bộ xử lý: sẵn sàng" : L"Bộ xử lý: đang chờ");
        InvalidateRect(get(start_stop),nullptr,TRUE);
    }
    void apply_status(const sd::Json& status) {
        const auto epoch=model_.device_epoch;model_.apply_status(status);update_output_states();rebuild_sources();update_enabled();
        if(epoch!=model_.device_epoch)client_->submit({sd::JobKind::refresh,{},"refresh"});
        summary();update_diagnostics();
    }
    void update_diagnostics() {
        if(page_!=1 || SendMessageW(get(freeze_diagnostics),BM_GETCHECK,0,0)==BST_CHECKED)return;
        auto data=model_.last_status;data["desktop_error"]=last_error_;data["desktop_version"]=SOUNDSHARE_VERSION;
        const auto bytes=data.dump(2);std::string lines;lines.reserve(bytes.size()+128);
        for(char byte:bytes){if(byte=='\n')lines+='\r';lines+=byte;}
        const auto text=widen(lines);const int line=static_cast<int>(SendMessageW(diagnostics_text_,EM_GETFIRSTVISIBLELINE,0,0));
        SetWindowTextW(diagnostics_text_,text.c_str());SendMessageW(diagnostics_text_,EM_LINESCROLL,0,line);
        unsigned running=0;std::uint64_t underruns=0;
        if(data.contains("engine"))for(const auto& item:data.at("engine").at("outputs")){if(item.value("state","")=="running")++running;underruns+=item.value("underruns",std::uint64_t{0});}
        SetWindowTextW(diagnostics_summary_,(L"Đầu ra đang chạy: "+std::to_wstring(running)+L"   ·   Lần thiếu đệm: "+std::to_wstring(underruns)).c_str());
    }
    void handle_reply(std::unique_ptr<sd::Reply> reply) {
        online_=reply->online;
        if(reply->restarted) {
            pending_session_=false;busy_sources_.clear();pending_outputs_.clear();pending_master_=false;
            model_.apply_status({{"engine",{{"active",false},{"outputs",sd::Json::array()}}},{"receivers",sd::Json::array()}});
            if(reply->data.contains("configuration") && reply->data.at("configuration").value("active",false))
                model_.adopt_session(reply->data.at("configuration").at("config"));
            client_->submit({sd::JobKind::refresh,{},"refresh"});
        }
        if(reply->job.kind==sd::JobKind::load)pending_profile_=false;
        if(reply->job.kind==sd::JobKind::command && (reply->job.method=="receiver.open" || reply->job.method=="receiver.close"))busy_sources_.erase(reply->job.params.value("id",""));
        if(reply->job.method=="session.start" || reply->job.method=="session.stop")pending_session_=false;
        if(!reply->error.empty()) {
            last_error_=reply->error.dump(2);notice(widen(sd::friendly_error(reply->error)),true);
            if(reply->job.kind==sd::JobKind::exit){closing_=false;SetTimer(hwnd,1,1000,nullptr);}
            update_enabled();update_diagnostics();return;
        }
        switch(reply->job.kind) {
        case sd::JobKind::bootstrap:case sd::JobKind::refresh:
        {
            const bool changed=reply->data.contains("inventory") && reply->data.at("inventory")!=last_inventory_;
            if(changed){last_inventory_=reply->data.at("inventory");model_.apply_inventory(last_inventory_);}
            if(reply->data.contains("sources"))model_.apply_sources(reply->data.at("sources"));
            if(reply->data.contains("configuration") && reply->data.at("configuration").value("active",false))model_.adopt_session(reply->data.at("configuration").at("config"));
            if(reply->data.contains("status"))model_.apply_status(reply->data.at("status"));
            if(changed || reply->job.kind==sd::JobKind::bootstrap)sync_controls();
            else {rebuild_sources();update_output_states();}
            if(reply->job.kind==sd::JobKind::bootstrap){banner_until_=0;summary();}
            if(reply->data.contains("source_error")){last_error_=reply->data.at("source_error").dump(2);notice(widen(sd::friendly_error(reply->data.at("source_error"))),true);}
            break;
        }
        case sd::JobKind::poll:apply_status(reply->data);break;
        case sd::JobKind::save:
            if(reply->job.path!=profile_path_)notice(L"Đã lưu cấu hình vào tệp bạn chọn.");break;
        case sd::JobKind::load:
            if(model_.active || pending_session_ || closing_)break;
            model_.load_profile(reply->data);sync_controls();send_profile(profile_path_);notice(L"Đã nạp cấu hình. Kiểm tra thiết bị rồi bắt đầu chia sẻ.");break;
        case sd::JobKind::exit:DestroyWindow(hwnd);return;
        case sd::JobKind::command:
            if(reply->job.method=="session.start"){
                if(reply->data.contains("engine"))apply_status({{"engine",reply->data.at("engine")},{"receivers",sd::Json::array()}});
                notice(L"Đang nạp âm thanh cho các đầu ra đã chọn.");
            } else if(reply->job.method=="session.stop"){
                model_.active=false;model_.all_running=false;
                model_.last_status={{"engine",{{"active",false},{"outputs",sd::Json::array()}}},{"receivers",sd::Json::array()}};
                model_.apply_sources(sd::Json::array());update_output_states();rebuild_sources();notice(L"Đã dừng chia sẻ và đóng các kết nối nhận.");
            } else if(reply->job.method=="receiver.open" || reply->job.method=="receiver.close"){
                model_.apply_sources(reply->data);rebuild_sources();notice(reply->job.method=="receiver.open" ? L"Đã gửi yêu cầu mở nhận. Theo dõi trạng thái thiết bị trước khi phát media." : L"Đã dừng nhận từ thiết bị.");
            } else if(reply->job.method=="diagnostics.export")notice(L"Đã xuất chẩn đoán. Tệp không chứa bản ghi âm.");
            client_->submit({sd::JobKind::poll,{},"poll"});break;
        }
        if(reply->restarted)notice(L"Bộ xử lý vừa khởi động lại. Kiểm tra thiết bị và bắt đầu lại phiên.",true);
        update_enabled();summary();update_diagnostics();
    }
    void toggle_output(const std::string& id,bool selected) {
        const auto previous=model_.output(id).selected;
        if(previous==selected)return;
        try {model_.set_selected(id,selected);}
        catch(...) {updating_=true;const auto found=std::find(output_ids_.begin(),output_ids_.end(),id);
            if(found!=output_ids_.end())ListView_SetCheckState(get(output_list),static_cast<int>(found-output_ids_.begin()),previous);updating_=false;throw;}
        if(model_.active) {
            if(selected && model_.available(id))command("output.add",sd::output_json(model_.output(id)));
            else if(!selected)command("output.remove",{{"id",id}});
        }
        save_later();update_output_states();banner_until_=0;summary();
    }
    void start_or_stop() {
        if(pending_session_ || pending_profile_ || closing_)return;
        if(model_.active)command("session.stop");
        else {
            const auto config=model_.configuration();command("session.start",{{"config",config}});
            if(model_.selected_count()>model_.selected_count(true))notice(L"Bắt đầu với thiết bị đang sẵn sàng; lựa chọn thiết bị vắng vẫn được lưu.");
        }
        pending_session_=true;update_enabled();save_later();
    }
    void request_exit() {
        if(closing_)return;closing_=true;KillTimer(hwnd,1);KillTimer(hwnd,2);KillTimer(hwnd,3);configure_timer_=false;pending_master_=false;pending_outputs_.clear();
        send_profile(profile_path_);client_->submit({sd::JobKind::exit,{},"exit"});notice(L"Đang dừng âm thanh và kết thúc ứng dụng…");update_enabled();
    }
    void click(int id) {
        if(closing_ && id!=exit_app && id!=hide_window)return;
        if(id>=nav_home && id<=nav_guide){page_=id-nav_home;update_visibility();update_diagnostics();InvalidateRect(hwnd,nullptr,TRUE);return;}
        switch(id) {
        case refresh_devices:client_->submit({sd::JobKind::refresh,{},"refresh"});notice(L"Đang cập nhật danh sách thiết bị…");break;
        case sound_settings:ShellExecuteW(hwnd,L"open",L"ms-settings:sound",nullptr,nullptr,SW_SHOWNORMAL);break;
        case bluetooth_settings:ShellExecuteW(hwnd,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);break;
        case start_stop:start_or_stop();break;
        case open_receiver:
            if(!model_.active || selected_source_.empty())return;
            busy_sources_.insert(selected_source_);command("receiver.open",{{"id",selected_source_},{"auto_reconnect",model_.auto_reconnect}});
            notice(L"Đang mở nhận âm thanh từ thiết bị…");update_enabled();break;
        case close_receiver:
            if(selected_source_.empty())return;
            busy_sources_.insert(selected_source_);command("receiver.close",{{"id",selected_source_}});update_enabled();break;
        case auto_reconnect:model_.auto_reconnect=SendMessageW(get(id),BM_GETCHECK,0,0)==BST_CHECKED;save_later();break;
        case output_mute:
            if(!selected_output_.empty()){model_.output(selected_output_).muted=SendMessageW(get(id),BM_GETCHECK,0,0)==BST_CHECKED;configure_later(selected_output_);save_later();update_output_states();}break;
        case only_output:
            if(selected_output_.empty() || model_.blocked(selected_output_))return;
            for(auto& output:model_.outputs)if(output.selected && output.id!=selected_output_)toggle_output(output.id,false);
            if(!model_.output(selected_output_).selected)toggle_output(selected_output_,true);
            updating_=true;rebuild_outputs();updating_=false;notice(L"Đã chọn chỉ phát trên thiết bị này.");break;
        case save_profile:{const auto path=choose_file(hwnd,true,L"Lưu cấu hình SoundShare");if(!path.empty())send_profile(path);break;}
        case load_profile:{if(model_.active || pending_session_ || pending_profile_)return;const auto path=choose_file(hwnd,false,L"Mở cấu hình SoundShare");if(!path.empty()){client_->submit({sd::JobKind::load,{},{},path});pending_profile_=true;update_enabled();}break;}
        case export_diagnostics:{const auto path=choose_file(hwnd,true,L"Xuất chẩn đoán SoundShare");if(!path.empty())command("diagnostics.export",{{"path",path}});break;}
        case freeze_diagnostics:update_diagnostics();break;
        case hide_window:ShowWindow(hwnd,SW_HIDE);break;
        case exit_app:request_exit();break;
        }
    }
    void combo_change(int id) {
        if(updating_)return;
        if(id==source_mode) {
            const std::string mode=SendMessageW(get(id),CB_GETCURSEL,0,0)==0 ? "loopback" : "capture";
            model_.set_capture(mode,"");
            const auto options=model_.capture_options();
            if(mode=="loopback" && !options.empty()) {
                const auto preferred=std::find_if(options.begin(),options.end(),[](const auto& item){return item.default_multimedia;});
                model_.set_capture(mode,preferred==options.end() ? options.front().id : preferred->id);
            }
            sync_controls();
        } else if(id==capture_device) {
            const auto index=SendMessageW(get(id),CB_GETCURSEL,0,0);
            if(index>=0 && static_cast<std::size_t>(index)<capture_options_.size())model_.set_capture(model_.mode,capture_options_[static_cast<std::size_t>(index)]);
            updating_=true;rebuild_outputs();updating_=false;update_selected_output();
        } else if(id==bridge_device) {
            const auto index=SendMessageW(get(id),CB_GETCURSEL,0,0);
            if(index>=0 && static_cast<std::size_t>(index)<bridge_options_.size())model_.bridge_render_id=bridge_options_[static_cast<std::size_t>(index)];
            model_.set_capture(model_.mode,model_.capture_id);updating_=true;rebuild_outputs();updating_=false;
        } else if(id==quality_mode) {
            const auto index=SendMessageW(get(id),CB_GETCURSEL,0,0);model_.buffer_ms=index==1 ? 240U : index==2 ? 80U : 120U;
        }
        save_later();
    }
    void on_timer(UINT_PTR id) {
        if(id==1 && !closing_) {
            ++ticks_;client_->submit({ticks_%15==0 ? sd::JobKind::refresh : sd::JobKind::poll,{},ticks_%15==0 ? "refresh" : "poll"});
        } else if(id==2) {KillTimer(hwnd,2);send_profile(profile_path_);}
        else if(id==3) {
            KillTimer(hwnd,3);configure_timer_=false;
            if(model_.active) {
                if(pending_master_)command("master.configure",{{"gain",model_.master_gain}},"master");
                for(const auto& output:pending_outputs_)if(model_.output(output).selected)command("output.configure",sd::output_json(model_.output(output)),"output:"+output);
            }
            pending_master_=false;pending_outputs_.clear();
        }
    }
    void add_tray() {
        tray_.cbSize=sizeof(tray_);tray_.hWnd=hwnd;tray_.uID=1;tray_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
        tray_.uCallbackMessage=tray_message;tray_.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        wcscpy_s(tray_.szTip,L"SoundShare — Chia sẻ âm thanh");tray_added_=Shell_NotifyIconW(NIM_ADD,&tray_)!=FALSE;
        if(tray_added_){tray_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray_);}
    }
    void tray_menu() {
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,tray_open,L"Mở SoundShare");
        AppendMenuW(menu,MF_STRING|(model_.active ? 0 : MF_GRAYED),tray_stop,L"Dừng chia sẻ");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,tray_exit,L"Thoát và dừng âm thanh");
        POINT point{};GetCursorPos(&point);SetForegroundWindow(hwnd);
        const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);PostMessageW(hwnd,WM_NULL,0,0);
        if(selected==tray_open){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);}
        else if(selected==tray_stop && model_.active)start_or_stop();else if(selected==tray_exit)request_exit();
    }
    void fill_card(HDC dc,RECT logical) {
        RECT rect{px(logical.left),px(logical.top),px(logical.right),px(logical.bottom)};
        auto pen=CreatePen(PS_SOLID,1,border);auto old_pen=SelectObject(dc,pen);auto old_brush=SelectObject(dc,white_brush_);
        RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,px(14),px(14));SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);
    }
    void paint() {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT area{};GetClientRect(hwnd,&area);FillRect(dc,&area,bg_brush_);
        RECT nav{0,0,px(188),area.bottom};auto brush=CreateSolidBrush(sidebar);FillRect(dc,&nav,brush);DeleteObject(brush);
        SetBkMode(dc,TRANSPARENT);SelectObject(dc,title_font_);SetTextColor(dc,white);
        RECT brand{px(22),px(34),px(180),px(69)};DrawTextW(dc,L"SoundShare",-1,&brand,DT_LEFT|DT_SINGLELINE);
        SelectObject(dc,small_);SetTextColor(dc,RGB(159,188,202));RECT tag{px(24),px(78),px(175),px(104)};DrawTextW(dc,L"Âm thanh, liền mạch.",-1,&tag,DT_LEFT|DT_SINGLELINE);
        if(page_==0){fill_card(dc,capture_card_);fill_card(dc,receiver_card_);fill_card(dc,outputs_card_);}else fill_card(dc,page_card_);
        fill_card(dc,footer_card_);EndPaint(hwnd,&paint);
    }
    LRESULT draw_button(DRAWITEMSTRUCT& item) {
        if(item.CtlType!=ODT_BUTTON)return FALSE;const int id=static_cast<int>(item.CtlID);const bool nav=id>=nav_home && id<=nav_guide;
        const bool hide=id==hide_window,primary=id==start_stop,disabled=(item.itemState&ODS_DISABLED)!=0;
        const COLORREF bg=nav ? (id-nav_home==page_ ? nav_active : sidebar) : hide ? sidebar : primary ? (model_.active ? danger : accent) : white;
        auto base_brush=CreateSolidBrush(nav || hide ? sidebar : white);FillRect(item.hDC,&item.rcItem,base_brush);DeleteObject(base_brush);
        auto brush=CreateSolidBrush(bg);auto pen=CreatePen(PS_SOLID,1,nav || hide || primary ? bg : border);
        auto old_brush=SelectObject(item.hDC,brush),old_pen=SelectObject(item.hDC,pen);
        RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,px(9),px(9));
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled ? RGB(151,163,175) : nav || hide || primary ? white : ink);
        SelectObject(item.hDC,nav || primary ? semibold_ : body_);auto text=control_text(item.hwndItem);RECT text_rect=item.rcItem;
        if(item.itemState&ODS_SELECTED)OffsetRect(&text_rect,1,1);
        DrawTextW(item.hDC,text.c_str(),-1,&text_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.itemState&ODS_FOCUS){InflateRect(&text_rect,-4,-4);DrawFocusRect(item.hDC,&text_rect);}
        SelectObject(item.hDC,old_brush);SelectObject(item.hDC,old_pen);DeleteObject(brush);DeleteObject(pen);return TRUE;
    }
    LRESULT notifications(NMHDR* header) {
        if(header->idFrom==output_list && header->code==LVN_ITEMCHANGED && !updating_) {
            const auto& change=*reinterpret_cast<NMLISTVIEW*>(header);
            if(change.iItem<0 || static_cast<std::size_t>(change.iItem)>=output_ids_.size())return 0;
            const auto id=output_ids_[static_cast<std::size_t>(change.iItem)];
            if((change.uNewState&LVIS_SELECTED) && !(change.uOldState&LVIS_SELECTED)){selected_output_=id;update_selected_output();update_enabled();}
            if((change.uChanged&LVIF_STATE) && ((change.uNewState^change.uOldState)&LVIS_STATEIMAGEMASK))toggle_output(id,ListView_GetCheckState(get(output_list),change.iItem)!=FALSE);
        } else if(header->idFrom==source_list && header->code==LVN_ITEMCHANGED && !updating_) {
            const auto& change=*reinterpret_cast<NMLISTVIEW*>(header);
            if(change.iItem>=0 && static_cast<std::size_t>(change.iItem)<source_ids_.size() && (change.uNewState&LVIS_SELECTED)){selected_source_=source_ids_[static_cast<std::size_t>(change.iItem)];update_enabled();}
        } else if(header->idFrom==output_list && header->code==NM_CUSTOMDRAW) {
            auto& draw=*reinterpret_cast<NMLVCUSTOMDRAW*>(header);
            if(draw.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw.nmcd.dwDrawStage==CDDS_ITEMPREPAINT && draw.nmcd.dwItemSpec<output_ids_.size()) {
                const auto& id=output_ids_[draw.nmcd.dwItemSpec];draw.clrText=model_.blocked(id) || !model_.available(id) ? muted : ink;draw.clrTextBk=white;return CDRF_NEWFONT;
            }
        }
        return 0;
    }
    LRESULT message(UINT message,WPARAM wp,LPARAM lp) {
        if(message==taskbar_created_){add_tray();return 0;}
        switch(message) {
        case WM_CREATE:initialize();return 0;
        case WM_SIZE:if(body_)layout();return 0;
        case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={px(1060),px(760)};return 0;}
        case WM_DPICHANGED:{dpi_=HIWORD(wp);make_fonts();const auto& rect=*reinterpret_cast<RECT*>(lp);SetWindowPos(hwnd,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;}
        case WM_PAINT:paint();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_DRAWITEM:return draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        case WM_CTLCOLORSTATIC:{const auto control=reinterpret_cast<HWND>(lp);const auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,OPAQUE);SetTextColor(dc,control==banner_ && banner_error_ ? danger : control==subtitle_ || control==banner_ || control==backend_label_ || control==receiver_hint_ || control==output_note_ || control==outputs_hint_ || control==source_hint_ ? muted : ink);
            const bool root=control==title_ || control==subtitle_ || control==banner_ || control==backend_label_;SetBkColor(dc,root ? background : white);return reinterpret_cast<LRESULT>(root ? bg_brush_ : white_brush_);}
        case WM_CTLCOLOREDIT:{const auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ink);SetBkColor(dc,white);return reinterpret_cast<LRESULT>(white_brush_);}
        case WM_NOTIFY:return notifications(reinterpret_cast<NMHDR*>(lp));
        case WM_COMMAND:{const int id=LOWORD(wp),event=HIWORD(wp);if(lp==0 || event==BN_CLICKED)click(id);else if(event==CBN_SELCHANGE)combo_change(id);
            else if(id==output_delay && event==EN_CHANGE && !updating_ && !selected_output_.empty()){
                const auto text=control_text(get(output_delay));if(text.empty())return 0;
                if(text.size()>4 || !std::all_of(text.begin(),text.end(),[](wchar_t value){return value>=L'0' && value<=L'9';})){
                    notice(L"Bù độ trễ cần là số nguyên từ 0 đến 1000 ms.",true);return 0;}
                unsigned value=0;for(auto digit:text)value=value*10+static_cast<unsigned>(digit-L'0');
                if(value>1000){notice(L"Bù độ trễ nằm trong khoảng 0–1000 ms.",true);return 0;}
                model_.output(selected_output_).delay_ms=value;configure_later(selected_output_);save_later();}
            return 0;}
        case WM_HSCROLL:if(!updating_){const auto control=reinterpret_cast<HWND>(lp);if(control==get(master_gain)){
                model_.master_gain=static_cast<float>(SendMessageW(control,TBM_GETPOS,0,0))/100;pending_master_=true;configure_later();
                SetWindowTextW(master_label_,(L"Âm lượng chung: "+std::to_wstring(std::lround(model_.master_gain*100))+L"%").c_str());
            }else if(control==get(output_gain) && !selected_output_.empty()){
                auto& output=model_.output(selected_output_);output.gain=static_cast<float>(SendMessageW(control,TBM_GETPOS,0,0))/100;
                SetWindowTextW(gain_label_,(L"Âm lượng: "+std::to_wstring(std::lround(output.gain*100))+L"%").c_str());configure_later(selected_output_);
            }save_later();}return 0;
        case WM_TIMER:on_timer(wp);return 0;
        case sd::reply_message:handle_reply(std::unique_ptr<sd::Reply>(reinterpret_cast<sd::Reply*>(lp)));return 0;
        case tray_message:{const auto event=LOWORD(lp);if(event==WM_CONTEXTMENU || event==WM_RBUTTONUP)tray_menu();
            else if(event==NIN_SELECT || event==NIN_KEYSELECT || event==WM_LBUTTONDBLCLK){ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);}return 0;}
        case WM_CLOSE:if(closing_)return 0;if(model_.active){ShowWindow(hwnd,SW_HIDE);return 0;}request_exit();return 0;
        case WM_QUERYENDSESSION:return TRUE;
        case WM_ENDSESSION:if(wp)DestroyWindow(hwnd);return 0;
        case WM_DESTROY:KillTimer(hwnd,1);KillTimer(hwnd,2);KillTimer(hwnd,3);if(tray_added_)Shell_NotifyIconW(NIM_DELETE,&tray_);if(client_)client_->stop();PostQuitMessage(0);return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    try {
        soundshare::check_hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"initialize desktop COM");
        struct Apartment {~Apartment(){CoUninitialize();}} apartment;
        const auto mutex_name=L"Local\\SoundShare.Desktop."+widen(soundshare::current_pipe_name());
        soundshare::Handle singleton(CreateMutexW(nullptr,FALSE,mutex_name.c_str()));
        if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=FindWindowW(window_class,nullptr)){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}return 0;}
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSEXW klass{};klass.cbSize=sizeof(klass);klass.hInstance=instance;klass.lpfnWndProc=Window::procedure;klass.lpszClassName=window_class;
        klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(nullptr,IDI_APPLICATION);klass.hIconSm=klass.hIcon;RegisterClassExW(&klass);
        Window app;const auto dpi=GetDpiForSystem();MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor);
        const int width=std::min<int>(MulDiv(1280,dpi,96),monitor.rcWork.right-monitor.rcWork.left-32);
        const int height=std::min<int>(MulDiv(850,dpi,96),monitor.rcWork.bottom-monitor.rcWork.top-32);
        HWND window=CreateWindowExW(0,window_class,L"SoundShare — Chia sẻ âm thanh",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+16,width,height,nullptr,nullptr,instance,&app);
        if(!window)throw std::runtime_error("Không tạo được cửa sổ SoundShare.");ShowWindow(window,show);UpdateWindow(window);
        const ACCEL entries[]={{FCONTROL|FVIRTKEY,'S',save_profile},{FCONTROL|FVIRTKEY,'O',load_profile},{FVIRTKEY,VK_F5,refresh_devices},{FCONTROL|FVIRTKEY,'Q',exit_app}};
        HACCEL accelerators=CreateAcceleratorTableW(const_cast<LPACCEL>(entries),static_cast<int>(std::size(entries)));
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0)if(!TranslateAcceleratorW(window,accelerators,&message) && !IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        DestroyAcceleratorTable(accelerators);return static_cast<int>(message.wParam);
    }catch(const std::exception& error){MessageBoxW(nullptr,widen(error.what()).c_str(),L"SoundShare",MB_OK|MB_ICONERROR);return 1;}
}
