// Grand Theft Completionist - Direct2D overlay for GTA classic/DE collectible tracking.
// Build: zig c++ main.cpp -O2 -o GrandTheftCompletionist.exe -ld2d1 -ldwrite -ldcomp -ld3d11 -ldxgi -lwindowscodecs -luuid -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -lwinmm
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

static void GTCLOG(const char* msg);
#define APP_SUBTITLE L"by w1kscc"
static void GTCLOGF(const char* fmt,...);
static LRESULT CALLBACK CaptureKbdProc(int,WPARAM,LPARAM);
static LRESULT CALLBACK CaptureMouseProc(int,WPARAM,LPARAM);
static void EndBindCapture();
#define APP_WND      L"GTCOverlayWnd"
#define APP_NAME     L"Grand Theft Completionist"
#define WM_TRAYICON  (WM_APP + 1)
#define HOTKEY_MAP   1
#define TIMER_UNCLIP 2
#define TIMER_TOPMOST 3
#define TIMER_UNIVERSE 4
#define TIMER_SAVECHECK 5
#define WM_BIND_VK   (WM_APP+2)
#define WM_TOGGLE    (WM_APP+3)
#define WM_BIND_END  (WM_APP+4)
#define MENU_PANEL_W 280.0f

template<class T> static void SafeRelease(T** p){ if(*p){ (*p)->Release(); *p=nullptr; } }
template<class T> static T tmin(T a,T b){ return a<b?a:b; }
template<class T> static T tmax(T a,T b){ return a>b?a:b; }
#define min tmin
#define max tmax
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static std::wstring ExeDir(){ wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr,b,MAX_PATH); std::wstring s=b; size_t p=s.find_last_of(L"\\"); return s.substr(0,p); }
static std::wstring AppDataDir(){ wchar_t* p=nullptr; std::wstring r; if(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&p)==S_OK){ r=p; CoTaskMemFree(p);} return r; }
static bool FileExists(const std::wstring& p){ DWORD a=GetFileAttributesW(p.c_str()); return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
static std::wstring Utf8ToWide(const std::string& s){ if(s.empty())return L""; int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),nullptr,0); std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.c_str(),(int)s.size(),&w[0],n); return w; }
static std::string WideToUtf8(const std::wstring& w){ if(w.empty())return ""; int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr); std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),&s[0],n,nullptr,nullptr); return s; }

// ---------------------------------------------------------------- tiny JSON
struct JValue;
static JValue JParse(const std::wstring& s,size_t& i);
struct JValue {
    wchar_t type=L'n';
    bool b=false; double num=0; std::wstring str;
    std::vector<JValue> arr;
    std::vector<std::pair<std::wstring,JValue>> obj;
    const JValue* get(const std::wstring& k) const { for(auto&kv:obj) if(kv.first==k) return &kv.second; return nullptr; }
};
static void JSkipWs(const std::wstring& s,size_t& i){ while(i<s.size()&&(s[i]==L' '||s[i]==L'\t'||s[i]==L'\n'||s[i]==L'\r'))i++; }
static JValue JParse(const std::wstring& s,size_t& i){
    JValue v; JSkipWs(s,i); if(i>=s.size()) return v;
    wchar_t c=s[i];
    if(c==L'{'){ v.type=L'o'; i++; while(i<s.size()){ JSkipWs(s,i); if(i<s.size()&&s[i]==L'}'){i++;break;} if(s[i]==L','){i++;continue;} JValue k=JParse(s,i); JSkipWs(s,i); if(i<s.size()&&s[i]==L':')i++; JValue val=JParse(s,i); v.obj.push_back({k.str,val}); } }
    else if(c==L'['){ v.type=L'a'; i++; while(i<s.size()){ JSkipWs(s,i); if(i<s.size()&&s[i]==L']'){i++;break;} if(s[i]==L','){i++;continue;} v.arr.push_back(JParse(s,i)); } }
    else if(c==L'"'){ v.type=L's'; i++; while(i<s.size()&&s[i]!=L'"'){ if(s[i]==L'\\'){ i++; if(i<s.size()){ if(s[i]==L'n')v.str+=L'\n'; else if(s[i]==L'"')v.str+=L'"'; else if(s[i]==L'\\')v.str+=L'\\'; else v.str+=s[i]; i++; } } else v.str+=s[i++]; } i++; }
    else if(c==L't'||c==L'f'){ v.type=L'b'; v.b=(c==L't'); while(i<s.size()&&s[i]!=L','&&s[i]!=L'}'&&s[i]!=L']')i++; }
    else { v.type=L'n'; std::wstring t; while(i<s.size()&&(iswdigit(s[i])||s[i]==L'-'||s[i]==L'+'||s[i]==L'.'||s[i]==L'e'||s[i]==L'E')) t+=s[i++]; v.num=_wtof(t.c_str()); }
    return v;
}
static std::wstring JEsc(const std::wstring& s){ std::wstring r; for(wchar_t c:s){ if(c==L'"')r+=L"\\\""; else if(c==L'\\')r+=L"\\\\"; else if(c==L'\n')r+=L"\\n"; else r+=c; } return r; }

// ---------------------------------------------------------------- data model
struct Spot { float x=0, y=0; int idx=0; };
struct Category { std::wstring name; std::vector<Spot> spots; };
struct GameDef {
    std::wstring id, title, colorHex;
    std::wstring dataDir, dataFile, mapFile;
    double sx=1,ox=0,sy=1,oy=0;   // world -> map bitmap px
    bool swapXY=false;            // true: px = y*sx+ox, py = -x*sy+oy (GTA III DE-координаты)
    std::vector<std::wstring> saveDirs;
    std::vector<std::wstring> saveExts;
    std::wstring savePrefix;      // если задан — файлы сейвов ищутся по префиксу (без расширения)
    bool hasEditions=true;        // есть ли переключатель Definitive/обычная
};
struct Cal { double dx=0, dy=0, scale=1.0; bool fx=false, fy=false; }; // пользовательская подгонка меток

struct App {
    HWND hwnd=nullptr;
    HWND previousForeground=nullptr; // окно игры, активное до открытия оверлея
    ID2D1Factory1* d2dFactory=nullptr;
    IWICBitmap* wicBmp=nullptr;
    ID2D1RenderTarget* rt=nullptr;
    UINT rtW=0, rtH=0;
    IDWriteFactory* dwrite=nullptr;
    IWICImagingFactory* wic=nullptr;
    IDWriteTextFormat* tfTitle=nullptr,*tfNorm=nullptr,*tfSmall=nullptr,*tfC=nullptr;

    std::vector<GameDef> games;
    int activeGame=0;
    bool definitiveEdition=true; // true: .sav (Definitive), false: .b (classic)
    bool mapVisible=true, showFound=false;
    std::vector<Category> cats;
    std::unordered_map<std::wstring,std::vector<int>> found;
    std::unordered_map<std::wstring,Cal> cals;
    std::unordered_map<std::wstring,Cal> mcals;   // калибровка САМОЙ КАРТЫ (поверх меток)
    std::unordered_map<std::wstring,D2D1_POINT_2F> spotOffs; // индивидуальный сдвиг одной метки (в px карты)
    std::unordered_map<std::wstring,int> hiddenCats;
    std::unordered_map<std::wstring,int> saveCounts; // прогресс коллекций из сейва (catName -> собрано)
    std::wstring savePath;              // актуальный файл сохранения (для синхронизации коллекций)
    FILETIME saveMtime{};               // время последнего изменения сейва

    ID2D1Bitmap* mapBmp=nullptr;
    ID2D1Bitmap* mapBmp2=nullptr;   // LOD 1/2
    ID2D1Bitmap* mapBmp4=nullptr;   // LOD 1/4
    std::wstring mapBmpFor;
    UINT mapW=0,mapH=0;
    std::unordered_map<std::wstring,ID2D1Bitmap*> iconCache;
    std::unordered_map<std::wstring,IWICBitmapSource*> iconWicCache; // декодированные один раз PNG-иконки
    UINT lastFollowW=0, lastFollowH=0; // окно игры, за которым следуем (для лога смены цели)

    POINT dragStart{}; // якорь драга калибровки (метки/карта)
    int popupCat=-1,popupIdx=-1;
    D2D1_RECT_F popupRect{};
    struct Btn { std::wstring id; D2D1_RECT_F r; };
    std::vector<Btn> buttons;
    std::wstring saveInfo;
    NOTIFYICONDATAW nid{}; HICON hIcon=nullptr;
    HDC dibDC=nullptr; HBITMAP dibHbm=nullptr; UINT dibW=0, dibH=0;
    bool capturingBind=false, saveFlash=false; UINT saveFlashUntil=0;
    UINT hotkeyVK=VK_OEM_3;
    bool mouseBind=false;            // бинд на кнопку мыши (активация через LL-хук)
    HHOOK hookKbd=nullptr, hookMouse=nullptr;      // постоянные хуки активации
    HHOOK capKbd=nullptr, capMouse=nullptr;        // хуки захвата при бинде
    bool uniOpen=false; float uniAnim=0.0f;
    bool calibMode=false, calibDrag=false, spotDrag=false;
    int dragSpotCat=-1, dragSpotIdx=-1;
    double spotDragScale=1; D2D1_POINT_2F spotDrag0{};
    int calTarget=0; // 0 = метки, 1 = карта
    double calDx0=0, calDy0=0;
} g;

static GameDef& CurGame(){ return g.games[g.activeGame]; }
// флаги «найдено» хранятся по игре+категории (иначе категории с одинаковыми именами
// в разных играх, например «Hidden Packages»/«Stunt Jumps», перетекают друг в друга)
static std::wstring FoundKey(const std::wstring& cat){ std::wstring k=CurGame().id; k+=L"|"; k+=cat; return k; }
static std::vector<int>& FoundFlags(const std::wstring& cat){ return g.found[FoundKey(cat)]; }

// ---------------------------------------------------------------- games config
static void LoadGames(){
    GameDef sa; sa.id=L"sa_de"; sa.title=L"GTA: San Andreas"; sa.colorHex=L"#8ec850";
    sa.dataDir=L"C:\\Users\\wiks\\Desktop\\gtasade"; sa.dataFile=L"gtasade.txt"; sa.mapFile=L"maps\\sa_map.png";
    sa.saveDirs={ L"%USERPROFILE%\\Documents\\Rockstar Games\\GTA San Andreas Definitive Edition",
                  L"%USERPROFILE%\\Documents\\GTA San Andreas User Files" };
    sa.saveExts={L".sav",L".b"};
    // Точная проекция MapGenie (z12-тайлы 2032..2047, 256px, upscaled 2x):
    // image_px = lng*2912.7111 + 4096 ; image_py = 4096 - lat*2912.7111
    sa.sx=2912.7111; sa.ox=2928.0; sa.sy=-2912.7111; sa.oy=2888.0; // MapGenie SA Atlas + маркеры MapGenie

    GameDef vc; vc.id=L"vc_de"; vc.title=L"GTA: Vice City"; vc.colorHex=L"#f06ec8";
    vc.dataDir=L"C:\\Users\\wiks\\Desktop\\gtavicede"; vc.dataFile=L"gtavicede.txt"; vc.mapFile=L"maps\\vc_map.png";
    vc.saveDirs={ L"%USERPROFILE%\\Documents\\Rockstar Games\\GTA Vice City Definitive Edition",
                  L"%USERPROFILE%\\Documents\\GTA Vice City User Files" };
    vc.saveExts={L".sav",L".b"};
    vc.sx=2912.7111; vc.ox=4096.0; vc.sy=-2912.7111; vc.oy=4096.0; // MapGenie VC Atlas + маркеры MapGenie

    GameDef g3; g3.id=L"gta3_de"; g3.title=L"GTA III"; g3.colorHex=L"#f0a050";
    g3.dataDir=L"C:\\Users\\wiks\\Desktop\\gta3de"; g3.dataFile=L"gta3de.txt"; g3.mapFile=L"maps\\gta3_map.png";
    g3.saveDirs={ L"%USERPROFILE%\\Documents\\Rockstar Games\\GTA III Definitive Edition",
                  L"%USERPROFILE%\\Documents\\GTA3 User Files" };
    g3.saveExts={L".sav",L".b"};
    g3.sx=2912.7111; g3.ox=4096.0; g3.sy=-2912.7111; g3.oy=4096.0; // MapGenie III Atlas + маркеры MapGenie

    GameDef g4; g4.id=L"gta4"; g4.title=L"GTA IV"; g4.colorHex=L"#d08030";
    g4.dataDir=L"C:\\Users\\wiks\\Desktop\\gta4"; g4.dataFile=L"gta4.txt"; g4.mapFile=L"maps\\gta4_map.png";
    g4.saveDirs={ L"%USERPROFILE%\\Documents\\Rockstar Games\\GTA IV\\Profiles",
                  L"%APPDATA%\\Goldberg SocialClub Emu Saves\\GTA IV" };
    g4.savePrefix=L"SG"; // SGTA4xx (IV) + SGLAD4xx (TLAD) + SGTBO4xx (TBOGT) // сейвы без расширения: SGTA400..SGTA412 + SGLAD4xx/SGTBO4xx (EFLC DLC)
    // EFLC DLC сейвы: SGLAD400.. (TLAD), SGTBO400.. (TBOGT) — тот же префикс SGT ловит их
    g4.hasEditions=false;
    g4.sx=2912.7111; g4.ox=4096.0; g4.sy=-2912.7111; g4.oy=4096.0; // MapGenie IV: lat/lng в градусах, меркатор (как у SA/VC/III)

    GameDef g5; g5.id=L"gta5"; g5.title=L"GTA V"; g5.colorHex=L"#62b04a";
    g5.dataDir=L"C:\\Users\\wiks\\Desktop\\gta5"; g5.dataFile=L"gta5.txt"; g5.mapFile=L"maps\\gta5_map.png";
    g5.saveDirs={ L"%USERPROFILE%\\Documents\\Rockstar Games\\GTA V\\Profiles",
                  L"%USERPROFILE%\\Documents\\Rockstar Games\\GTAV Enhanced\\Profiles",
                  L"%USERPROFILE%\\Documents\\GTAV Enhanced\\Profiles",
                  L"%PUBLIC%\\Documents\\SocialClub\\RUNE\\GTAV Enhanced" };
    g5.savePrefix=L"SGTA5"; // сейвы без расширения: SGTA50000...
    g5.hasEditions=false;
    // данные в нормализованных меркатор-координатах [0..1]; карта 4096px склеена из
    // тайлов z7 (43 шт. = 11008px мира z7 = 32768px), фактор 4096/11008
    g5.sx=12194.79; g5.ox=0.0; g5.sy=12194.79; g5.oy=0.0;

    g.games={sa,vc,g3,g4,g5};
    // самодостаточность: если рядом с exe есть data\<id>\ — используем её, иначе пути на Desktop
    for(auto&gd:g.games){
        // варианты имени папки: data\<id> (sa_de) и data\<имя файла данных> (gtasade)
        std::wstring stem=gd.dataFile; size_t dot=stem.rfind(L'.'); if(dot!=std::wstring::npos) stem.erase(dot);
        for(const std::wstring& name:{gd.id,stem}){
            std::wstring local=ExeDir()+L"\\data\\"+name;
            if(FileExists(local+L"\\"+gd.dataFile)){ gd.dataDir=local; break; }
        }
        if(!FileExists(gd.dataDir+L"\\"+gd.dataFile))
            GTCLOGF("data: НЕ найден %ls\\%ls (игра %ls)",gd.dataDir.c_str(),gd.dataFile.c_str(),gd.id.c_str());
    }
}
static std::wstring KeyName(UINT vk){
    wchar_t b[32];
    if(vk==VK_LBUTTON) return L"ЛКМ";
    if(vk==VK_RBUTTON) return L"ПКМ";
    if(vk==VK_MBUTTON) return L"СКМ";
    if(vk==VK_XBUTTON1) return L"Мышь X1";
    if(vk==VK_XBUTTON2) return L"Мышь X2";
    if(vk==VK_OEM_3) return L"ё / `";
    if(vk>=VK_F1&&vk<=VK_F12){ swprintf(b,32,L"F%d",vk-VK_F1+1); return b; }
    if(vk>=0x30&&vk<=0x39){ swprintf(b,32,L"%c",(char)vk); return b; }
    if(vk>=0x41&&vk<=0x5A){ swprintf(b,32,L"%c",(char)vk); return b; }
    switch(vk){
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_BACK: return L"Backspace";
    case VK_ESCAPE: return L"Esc";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return L"Shift";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return L"Ctrl";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return L"Alt";
    case VK_CAPITAL: return L"Caps Lock";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_LEFT: return L"Стрелка ←";
    case VK_RIGHT: return L"Стрелка →";
    case VK_UP: return L"Стрелка ↑";
    case VK_DOWN: return L"Стрелка ↓";
    case VK_NUMPAD0: case VK_NUMPAD1: case VK_NUMPAD2: case VK_NUMPAD3: case VK_NUMPAD4:
    case VK_NUMPAD5: case VK_NUMPAD6: case VK_NUMPAD7: case VK_NUMPAD8: case VK_NUMPAD9:
        swprintf(b,32,L"Num %d",(int)vk-VK_NUMPAD0); return b;
    case VK_MULTIPLY: return L"Num *";
    case VK_ADD: return L"Num +";
    case VK_SUBTRACT: return L"Num -";
    case VK_DECIMAL: return L"Num .";
    case VK_DIVIDE: return L"Num /";
    case VK_OEM_1: return L";";
    case VK_OEM_PLUS: return L"+";
    case VK_OEM_COMMA: return L",";
    case VK_OEM_MINUS: return L"-";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_2: return L"/";
    case VK_OEM_4: return L"[";
    case VK_OEM_5: return L"\\";
    case VK_OEM_6: return L"]";
    case VK_OEM_7: return L"'";
    }
    // последняя надежда — системное имя; отсеиваем пустые/управляющие символы
    if(GetKeyNameTextW(MAKELPARAM(0,(LONG)MapVirtualKeyW(vk,MAPVK_VK_TO_VSC)<<16),b,32)){
        std::wstring r=b;
        bool ok=!r.empty();
        for(wchar_t c:r) if((unsigned)c<0x20||(unsigned)c==0x7F) ok=false;
        if(ok) return r;
    }
    swprintf(b,32,L"0x%02X",vk); return b;
}
static std::wstring Expand(const std::wstring& p){ wchar_t b[MAX_PATH*2]; if(ExpandEnvironmentStringsW(p.c_str(),b,MAX_PATH*2)) return b; return p; }

static void FindNewestSave(){
    GameDef& gd=CurGame();
    // Resolve the current Windows user's Documents folder (no hard-coded account name).
    std::wstring documents;
    PWSTR knownDocs=nullptr;
    if(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&knownDocs)==S_OK && knownDocs){
        documents=knownDocs;
        CoTaskMemFree(knownDocs);
    }
    if(documents.empty()){
        wchar_t userProfile[32768]={0};
        DWORD n=GetEnvironmentVariableW(L"USERPROFILE",userProfile,(DWORD)_countof(userProfile));
        if(n>0 && n<_countof(userProfile)) documents=std::wstring(userProfile)+L"\\Documents";
    }

    const std::wstring ext = g.definitiveEdition ? L".sav" : L".b";
    std::wstring root;
    if(gd.id==L"gta4"||gd.id==L"gta5"){
        root.clear(); // ищем по всем saveDirs
    } else if(g.definitiveEdition){
        if(gd.id==L"gta3_de") root=documents+L"\\Rockstar Games\\GTA III Definitive Edition\\Profiles";
        else if(gd.id==L"vc_de") root=documents+L"\\Rockstar Games\\GTA Vice City Definitive Edition\\Profiles";
        else root=documents+L"\\Rockstar Games\\GTA San Andreas Definitive Edition\\Profiles";
    } else {
        if(gd.id==L"gta3_de") root=documents+L"\\GTA3 User Files";
        else if(gd.id==L"vc_de") root=documents+L"\\GTA Vice City User Files";
        else root=documents+L"\\GTA San Andreas User Files";
    }

    std::wstring bestPath; FILETIME bestTime{}; bool has=false;
    std::error_code ec;
    auto consider=[&](const std::wstring& path){
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&fad)) return;
        if(!has || CompareFileTime(&fad.ftLastWriteTime,&bestTime)>0){
            bestTime=fad.ftLastWriteTime; bestPath=path; has=true;
        }
    };
    auto matchSave=[&](const std::wstring& name)->bool{
        if(!gd.savePrefix.empty()){
            if(name.size()>=gd.savePrefix.size() && 0==wcsncmp(name.c_str(),gd.savePrefix.c_str(),gd.savePrefix.size())) return true;
            return false;
        }
        if(name.size()<=ext.size()) return false;
        std::wstring actualExt=std::filesystem::path(name).extension().wstring();
        std::transform(actualExt.begin(),actualExt.end(),actualExt.begin(),towlower);
        return actualExt==ext;
    };
    if(gd.id==L"gta4"||gd.id==L"gta5"){
        // все сохранённые каталоги, рекурсивно; плюс варианты через системную
        // папку «Документы» (на случай, если Документы перенесены, напр. в OneDrive)
        std::vector<std::wstring> dirs=gd.saveDirs;
        if(gd.id==L"gta4") dirs.push_back(L"%DOCS%\\Rockstar Games\\GTA IV\\Profiles");
        if(gd.id==L"gta5"){
            dirs.push_back(L"%DOCS%\\Rockstar Games\\GTA V\\Profiles");
            dirs.push_back(L"%DOCS%\\Rockstar Games\\GTAV Enhanced\\Profiles");
            dirs.push_back(L"%DOCS%\\GTAV Enhanced\\Profiles");
        }
        for(auto&d:dirs){
            std::wstring dir=Expand(d);
            if(dir.rfind(L"%DOCS%",0)==0){ // подстановка FOLDERID_Documents
                if(!documents.empty()) dir=documents+dir.substr(6);
                else continue;
            }
            if(!std::filesystem::exists(std::filesystem::path(dir),ec)||ec) continue;
            std::filesystem::recursive_directory_iterator it(std::filesystem::path(dir),std::filesystem::directory_options::skip_permission_denied,ec), last;
            for(; !ec && it!=last; it.increment(ec)){
                std::error_code sec;
                if(!it->is_regular_file(sec)||sec) continue;
                if(matchSave(it->path().filename().wstring())) consider(it->path().wstring());
            }
        }
    } else if(!root.empty() && std::filesystem::exists(std::filesystem::path(root),ec) && !ec){
        // Both editions are searched recursively; DE saves are commonly nested under profile IDs.
        std::filesystem::recursive_directory_iterator it(std::filesystem::path(root),std::filesystem::directory_options::skip_permission_denied,ec), last;
        for(; !ec && it!=last; it.increment(ec)){
            std::error_code sec;
            if(!it->is_regular_file(sec) || sec) continue;
            std::wstring name=it->path().filename().wstring();
            if(matchSave(name)) consider(it->path().wstring());
        }
    }
    if(!has){
        GTCLOGF("save: не найдено (game=%ls de=%d root=%ls)",gd.id.c_str(),g.definitiveEdition?1:0,root.c_str());
        g.saveInfo=L"не найдено";
        g.savePath.clear();
        g.saveMtime={};
        return;
    }
    g.savePath=bestPath;
    g.saveMtime=bestTime;
    GTCLOGF("save: найден %ls",bestPath.c_str());
    SYSTEMTIME st{}; FileTimeToSystemTime(&bestTime,&st);
    std::wstring fn=bestPath.substr(bestPath.find_last_of(L"\\/")+1);
    wchar_t buf[MAX_PATH+64];
    swprintf(buf,MAX_PATH+64,L"%s\n%02d.%02d.%04d %02d:%02d",fn.c_str(),st.wDay,st.wMonth,st.wYear,st.wHour,st.wMinute);
    g.saveInfo=buf;
}

// ---------------------------------------------------------------- save sync
// Синхронизация коллекций с файлом сохранения (.sav Definitive / .b классика).
// Сейв — источник истины: найденное в игре помечается «найдено» и скрывается с карты.
static uint32_t Rd32(const std::vector<uint8_t>& b,size_t o){
    if(o+4>b.size()) return 0;
    uint32_t v; memcpy(&v,&b[o],4); return v;
}
static uint16_t Rd16(const std::vector<uint8_t>& b,size_t o){
    if(o+2>b.size()) return 0;
    uint16_t v; memcpy(&v,&b[o],2); return v;
}
static float RdFl(const std::vector<uint8_t>& b,size_t o){
    if(o+4>b.size()) return 0;
    float v; memcpy(&v,&b[o],4); return v;
}
static int32_t RdS32(const std::vector<uint8_t>& b,size_t o){
    if(o+4>b.size()) return 0;
    int32_t v; memcpy(&v,&b[o],4); return v;
}
static Category* FindCatSub(const wchar_t* sub){
    for(auto&c:g.cats) if(wcsstr(c.name.c_str(),sub)) return &c;
    return nullptr;
}
// счётчик из сейва (без пер-предметных флагов — какие именно собраны, сейв не говорит)
static void MarkCountFromSave(Category& c,int n){
    g.saveCounts[c.name]=tmax(0,n);
    // счётчик = «первые N предметов найдены»: заполняем флаги, иначе карта не прячет иконки
    auto&fl=FoundFlags(c.name);
    if((int)fl.size()<(int)c.spots.size()) fl.resize(c.spots.size(),0);
    int nn=tmin(tmax(0,n),(int)c.spots.size());
    for(int i=0;i<(int)c.spots.size();i++) fl[i]=i<nn?1:0;
}
// пометить пер-предметные флаги (массив байт/булев) из сейва
static void MarkFlagsFromSave(Category& c,const std::vector<int>& flags){
    auto&fl=FoundFlags(c.name);
    if((int)fl.size()<(int)c.spots.size()) fl.resize(c.spots.size(),0);
    int n=tmin((int)flags.size(),(int)c.spots.size());
    for(int i=0;i<n;i++) fl[i]=flags[i]?1:0;
    int cnt=0; for(int f:flags) cnt+=f?1:0;
    g.saveCounts[c.name]=cnt;
}

// ---- координаты: наши данные -> мировые единицы (фит по реальным сейвам) ----
struct P2D { double x,y; };
static double SA_WX(double x){ return x*10930.2761+7817.3; }
static double SA_WY(double y){ return y*10965.5977-7702.9; }
static double G3_WX(double x){ return x*5335.3418+3802.5; }
static double G3_WY(double y){ return y*5334.2359-4339.9; }
// порядок тегов SA: наши данные -> индекс байта в TAGS (порядок = IPL-загрузка, как в редакторах)
static const int SA_TAG_PERM[100]={86,67,66,85,84,76,65,68,72,69,70,71,36,34,35,22,28,29,33,27,
    26,25,23,30,31,19,15,16,17,18,11,32,12,13,20,89,88,1,0,5,
    4,3,43,14,21,42,41,40,24,37,38,46,45,52,54,55,56,60,61,59,
    58,57,81,50,48,49,82,83,39,53,47,87,91,95,94,98,99,90,96,2,
    7,9,6,10,8,97,73,77,74,75,80,63,64,44,92,93,51,79,78,62,
    };
// пометить «собрано = спот без живого пикапа рядом» (оба набора — в мировых координатах)
static void MarkInverseLiveW(Category* c,const std::vector<P2D>& liveWorld,double thr,const std::vector<P2D>& spotsWorld){
    auto&fl=FoundFlags(c->name);
    if((int)fl.size()<(int)c->spots.size()) fl.resize(c->spots.size(),0);
    int cnt=0;
    for(size_t i=0;i<spotsWorld.size();i++){
        bool liveHit=false;
        for(auto&w:liveWorld){
            double dx=w.x-spotsWorld[i].x, dy=w.y-spotsWorld[i].y;
            if(dx*dx+dy*dy<thr*thr){ liveHit=true; break; }
        }
        fl[i]=liveHit?0:1;
        if(!liveHit) cnt++;
    }
    g.saveCounts[c->name]=cnt;
}

// классический SA (.b): блоки с маркером "BLOCK", блок 20 = теги, 24 = прыжки, 16 = статистика
static bool SyncClassicSA(const std::vector<uint8_t>& b){
    std::vector<size_t> m;
    for(size_t i=0;i+5<=b.size();i++) if(!memcmp(&b[i],"BLOCK",5)) m.push_back(i);
    if(m.size()<28||b.size()<0x30000) return false;
    auto blk=[&](size_t idx,size_t off)->size_t{ return m[idx]+5+off; };
    // валидация: блок 24 (USJ) должен быть 4+70*0x44
    if(Rd32(b,blk(24,0))!=70) return false;
    if(Category* c=FindCatSub(L"Tag")){
        std::vector<int> fl(100,0);
        // порядок байтов в TAGS = IPL-порядок; наши данные — в другом порядке (SA_TAG_PERM)
        for(int i=0;i<100;i++){
            int bi=SA_TAG_PERM[i];
            if(bi<0||bi>=100) continue;
            fl[i]=b[blk(20,4+bi)]>=229?1:0; // 229+ = закрашено (как в редакторах)
        }
        MarkFlagsFromSave(*c,fl);
    }
    if(Category* c=FindCatSub(L"Stunt")){
        // порядок записей прыжков не гарантирован — сопоставляем по координатам
        std::vector<int> fl((int)c->spots.size(),0);
        int cnt=0;
        for(size_t i=0;i<c->spots.size();i++){
            double wx=SA_WX(c->spots[i].x), wy=SA_WY(c->spots[i].y);
            for(int j=0;j<70;j++){
                size_t o=blk(24,4+j*0x44);
                float ax=RdFl(b,o),ay=RdFl(b,o+4),bx2=RdFl(b,o+12),by2=RdFl(b,o+16);
                double cx=((ax+bx2)*0.5-wx),cy=((ay+by2)*0.5-wy);
                if(cx*cx+cy*cy<50.0*50.0){ fl[i]=b[o+0x40]?1:0; if(fl[i])cnt++; break; }
            }
        }
        auto&flv=FoundFlags(c->name);
        if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
        for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
        g.saveCounts[c->name]=cnt;
    }
    // Snapshots/Horseshoes/Oysters: per-item через Block 6 (Pickups), 620×32б
    // модель 953=oyster, 954=horseshoe, 1253=snapshot; флаги +0x1D (0x00=живой)
    // позиция: int16 x,y на +0x10/+0x12 (делить на 8 = мировые координаты)
    {
        size_t b6=m[6]+5; // блок 6 (0-based index 6 = 7-й BLOCK)
        struct PK { uint16_t model; const wchar_t* cat; long ctr; };
        PK pks[]={ {953,L"Oyster",(long)Rd32(b,blk(16,0x334))},
                   {954,L"Horseshoe",(long)Rd32(b,blk(16,0x32c))},
                   {1253,L"Snapshot",(long)Rd32(b,blk(16,0x304))} };
        for(auto&pk:pks){
            Category* c=FindCatSub(pk.cat);
            if(!c) continue;
            std::vector<P2D> wl;
            for(int i=0;i<620;i++){
                size_t e=b6+(size_t)i*0x20;
                if(e+0x20>b.size()) break;
                uint16_t mdl; memcpy(&mdl,&b[e+0x18],2);
                if(mdl!=pk.model) continue;
                uint8_t flags=b[e+0x1D];
                if(flags!=0x00) continue; // только живые (не собранные)
                int16_t ix,iy; memcpy(&ix,&b[e+0x10],2); memcpy(&iy,&b[e+0x12],2);
                wl.push_back({ix/8.0,iy/8.0});
            }
            if(!wl.empty()){
                std::vector<P2D> dw;
                for(auto&s:c->spots) dw.push_back({SA_WX(s.x),SA_WY(s.y)});
                // валидация: количество живых должно быть spots.size() - counter
                int matched=0;
                for(auto&s:dw){
                    for(auto&w:wl){
                        double dx=w.x-s.x,dy=w.y-s.y;
                        if(dx*dx+dy*dy<60.0*60.0){matched++;break;}
                    }
                }
                int expected=(int)c->spots.size()-matched;
                if(expected==pk.ctr){
                    MarkInverseLiveW(c,wl,60.0,dw);
                } else {
                    GTCLOGF("sa_classic: %ls live=%zu matched=%d ctr=%ld -> счётчик",(const wchar_t*)pk.cat,wl.size(),matched,pk.ctr);
                    MarkCountFromSave(*c,(int)pk.ctr);
                }
            } else if(pk.ctr>=0){
                MarkCountFromSave(*c,(int)pk.ctr);
            }
        }
    }
    return true;
}

// классические III/VC (.b): блоки [size][data][size]...
struct IVCBlocks { std::vector<size_t> offs; std::vector<uint32_t> sizes; };
static bool WalkBlocksIVC(const std::vector<uint8_t>& b,IVCBlocks& out){
    out.offs.clear(); out.sizes.clear();
    size_t p=0;
    while(p+4<=b.size()){
        uint32_t sz=Rd32(b,p); p+=4;
        if(sz==0||sz>0x80000) break;
        out.offs.push_back(p); out.sizes.push_back(sz); p+=sz;
        // завершающий размер опционален: DE-подобные сейвы имеют, классика III/VC — нет
        if(p+4<=b.size()&&Rd32(b,p)==sz) p+=4;
    }
    return out.offs.size()>=19;
}
static bool gdIsVC=false;
// классические III/VC (.b): пакеты по живым пикапам (III), прыжки/rampages — счётчики
static bool SyncIVCPackages(const std::vector<uint8_t>& b,const IVCBlocks& bl,int playerIdx,int statIdx,
                            size_t pickupsIdx,size_t entSize,size_t posOff,size_t pickedOff,size_t modelOff,int total){
    size_t pi=bl.offs[playerIdx];
    int got=(int)Rd32(b,pi+0x13);
    if(got<0||got>total) return false;
    if(Category* c=FindCatSub(L"Package")){
        g.saveCounts[c->name]=got;
        if(!gdIsVC){
            // III: собранный пакет = слот пикапа освобождён; несобранные живы (type!=0)
            // модель самокалибруем: у живых пакетных пикапов type==5, у оружия другой type
            size_t pb=bl.offs[pickupsIdx];
            std::unordered_map<uint16_t,int> liveCnt;
            for(size_t i=0;i<336;i++){
                size_t e=pb+i*entSize;
                if(e+entSize>b.size()||e+entSize>pb+bl.sizes[pickupsIdx]) break;
                if(b[e+0]==5){ // type 5 = collectible-пикап (пакет)
                    uint16_t model=Rd16(b,e+modelOff);
                    if(model) liveCnt[model]++;
                }
            }
            // всего пакетов = total; живых должно быть total-got
            uint16_t model=0; int want=total-got;
            for(auto&kv:liveCnt) if(kv.second==want&&want>0){ model=kv.first; break; }
            if(model&&want>0){
                std::vector<P2D> live;
                for(size_t i=0;i<336;i++){
                    size_t e=pb+i*entSize;
                    if(e+entSize>b.size()||e+entSize>pb+bl.sizes[pickupsIdx]) break;
                    if(b[e+0]==5&&Rd16(b,e+modelOff)==model){
                        float x,y; memcpy(&x,&b[e+posOff],4); memcpy(&y,&b[e+posOff+4],4);
                        live.push_back({x,y});
                    }
                }
                // пикапы уже мировые; споты данных -> мир, «собрано» = нет живого пикапа в радиусе 60
                std::vector<P2D> dw;
                for(auto&s:c->spots) dw.push_back({G3_WX(s.x),G3_WY(s.y)});
                MarkInverseLiveW(c,live,60.0,dw);
            } else if(want==0&&got>0){
                // всё собрано
                auto&fl=FoundFlags(c->name);
                if((int)fl.size()<(int)c->spots.size()) fl.resize(c->spots.size(),0);
                for(size_t i=0;i<c->spots.size();i++) fl[i]=1;
            }
        }
    }
    if(Category* c=FindCatSub(L"Stunt")){
        size_t st=bl.offs[statIdx];
        g.saveCounts[c->name]=(int)Rd32(b,st+0xB0); // III
        if(gdIsVC) g.saveCounts[c->name]=(int)Rd32(b,st+0xBC);
    }
    if(Category* c=FindCatSub(L"Rampage")){
        g.saveCounts[c->name]=(int)Rd32(b,bl.offs[statIdx]+0x108); // III
        if(gdIsVC) g.saveCounts[c->name]=(int)Rd32(b,bl.offs[statIdx]+0x11C);
    }
    if(gdIsVC){ if(Category* c=FindCatSub(L"Rober")) g.saveCounts[c->name]=(int)RdFl(b,bl.offs[statIdx]+0x140); }
    return true;
}

// ---- GTA IV: сейвы SGTA4xx (открытые, 32 блока "BLOCK", размер с +5) ----
static double G4_WX(double x){ return x*5705.3261+4014.8; }  // данные -> мир (фит по пикапам голубей)
static double G4_WY(double y){ return y*5705.0620-3504.2; }
static bool SyncGTA4(const std::vector<uint8_t>& b){
    std::vector<size_t> bs; size_t p=0x110;
    while(p+9<=b.size()&&!memcmp(&b[p],"BLOCK",5)&&bs.size()<36){ bs.push_back(p); p+=Rd32(b,p+5); }
    if(bs.size()<16) return false;
    // Episode detection: ScriptSpaceSize (3rd dword @0x08)
    // TLAD=39279, TBOGT=43847, IV=other
    uint32_t sss=Rd32(b,0x08);
    bool isTLAD=(sss==39279), isTBOGT=(sss==43847);
    GTCLOGF("gta4: episode %s (sss=%u)",isTLAD?"TLAD":(isTBOGT?"TBOGT":"IV"),sss);
    // Определить модель и категорию
    int pigeonModel=2268, tladModel=431, tbogtModel=1396;
    int model=0; Category* cat=nullptr;
    const wchar_t* epName=L"IV";
    if(isTLAD){ model=tladModel; cat=FindCatSub(L"Seagull"); epName=L"TLAD";
        // найти конкретно TLAD категорию
        for(auto&cc:g.cats) if(cc.name.find(L"TLAD")!=std::wstring::npos){ cat=&cc; break; }
    }
    else if(isTBOGT){ model=tbogtModel; cat=FindCatSub(L"Seagull"); epName=L"TBOGT";
        for(auto&cc:g.cats) if(cc.name.find(L"TBOGT")!=std::wstring::npos){ cat=&cc; break; }
    }
    else { model=pigeonModel; cat=FindCatSub(L"Pigeon"); }
    if(!cat) { GTCLOGF("gta4: no category for %s",epName); }
    // Голуби/Чайки: блок 7, пикапы 650 x 0x54
    if(cat){
        size_t b7=bs[7];
        std::unordered_map<int,int> liveCnt;
        for(int i=0;i<650;i++){
            size_t e=b7+13+(size_t)i*0x54;
            if(e+0x54>b.size()||RdS32(b,e)==-1) continue;
            int16_t obj; memcpy(&obj,&b[e+0x44],2);
            liveCnt[obj]++;
        }
        if(!liveCnt.count(model)){
            int best=0; for(auto&kv:liveCnt) if(kv.second>best&&kv.second<=200){best=kv.second;model=kv.first;}
            if(best<50) model=0;
        }
        std::vector<P2D> wl;
        if(model){
            for(int i=0;i<650;i++){
                size_t e=b7+13+(size_t)i*0x54;
                if(e+0x54>b.size()||RdS32(b,e)==-1) continue;
                int16_t obj; memcpy(&obj,&b[e+0x44],2);
                if(obj!=model) continue;
                float x,y; memcpy(&x,&b[e+0x20],4); memcpy(&y,&b[e+0x24],4);
                wl.push_back({x,y}); // пикапы уже в мировых координатах
            }
        }
        std::vector<P2D> dw;
        for(auto&s:cat->spots) dw.push_back({G4_WX(s.x),G4_WY(s.y)});
        MarkInverseLiveW(cat,wl,60.0,dw);
    }
    // Трюковые прыжки: блок 15 (присутствует во всех эпизодах)
    { Category* c=FindCatSub(L"Stunt"); if(c&&bs.size()>15){
        int completed=RdS32(b,bs[15]+5);
        int total=RdS32(b,bs[15]+9);
        if(total>=40&&total<=64){
            std::vector<int> fl((int)c->spots.size(),0);
            int cnt=0;
            for(size_t i=0;i<c->spots.size()&&i<(size_t)total;i++){
                size_t st=bs[15]+0x11+i*0x64;
                if(b[st+0x58]){ fl[i]=1; cnt++; }
            }
            if(cnt!=completed){
                GTCLOGF("gta4: jumps flags=%d counter=%d -> счётчик",cnt,completed);
                MarkCountFromSave(*c,completed);
            } else {
                auto&flv=FoundFlags(c->name);
                if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
                for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
                g.saveCounts[c->name]=cnt;
            }
        }
    } }
    return true;
}
// ---- GTA V: AES-256-ECB (публичный ключ) + PSIN + статы по joaat ----
struct Aes256 {
    uint32_t rk[60]; // 15 round keys (big-endian слова)
    static const uint8_t* sbox(){ static const uint8_t t[256]={
        0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
        0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
        0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
        0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
        0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
        0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
        0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
        0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
        0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
        0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
        0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
        0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
        0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
        0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
        0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
        0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
        return t; }
    static const uint8_t* invsbox(){ static uint8_t t[256]; static bool init=false;
        if(!init){ const uint8_t* s=sbox(); for(int i=0;i<256;i++) t[s[i]]=(uint8_t)i; init=true; } return t; }
    static uint8_t xtime(uint8_t x){ return (uint8_t)((x<<1)^((x>>7)*0x1b)); }
    static uint8_t gmul(uint8_t a,uint8_t b){ uint8_t r=0; while(b){ if(b&1) r^=a; a=xtime(a); b>>=1; } return r; }
    Aes256(const uint8_t key[32]){
        for(int i=0;i<8;i++)
            rk[i]=((uint32_t)key[4*i]<<24)|((uint32_t)key[4*i+1]<<16)|((uint32_t)key[4*i+2]<<8)|key[4*i+3];
        static const uint8_t rcon[10]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
        const uint8_t* sb=sbox();
        for(int i=8;i<60;i++){
            uint32_t t=rk[i-1];
            if(i%8==0){
                t=((t<<8)|(t>>24));
                t=((uint32_t)sb[(t>>24)&0xff]<<24)|((uint32_t)sb[(t>>16)&0xff]<<16)|((uint32_t)sb[(t>>8)&0xff]<<8)|sb[t&0xff];
                t^=((uint32_t)rcon[i/8-1]<<24);
            } else if(i%8==4){
                t=((uint32_t)sb[(t>>24)&0xff]<<24)|((uint32_t)sb[(t>>16)&0xff]<<16)|((uint32_t)sb[(t>>8)&0xff]<<8)|sb[t&0xff];
            }
            rk[i]=rk[i-8]^t;
        }
    }
    void addRound(uint8_t* s,int r) const {
        for(int c=0;c<4;c++){
            uint32_t k=rk[4*r+c];
            s[4*c+0]^=(uint8_t)(k>>24); s[4*c+1]^=(uint8_t)(k>>16);
            s[4*c+2]^=(uint8_t)(k>>8);  s[4*c+3]^=(uint8_t)k;
        }
    }
    void invCipher(uint8_t* s) const {
        addRound(s,14);
        for(int round=13;round>=1;round--){
            // InvShiftRows: строка r сдвигается вправо на r (столбцовый порядок s[4c+r])
            for(int r=1;r<4;r++){
                uint8_t row[4];
                for(int c=0;c<4;c++) row[c]=s[4*c+r];
                for(int c=0;c<4;c++) s[4*((c+r)%4)+r]=row[c];
            }
            const uint8_t* isb=invsbox();
            for(int i=0;i<16;i++) s[i]=isb[s[i]];
            addRound(s,round);
            for(int c=0;c<4;c++){
                uint8_t a0=s[4*c],a1=s[4*c+1],a2=s[4*c+2],a3=s[4*c+3];
                s[4*c]  =gmul(a0,14)^gmul(a1,11)^gmul(a2,13)^gmul(a3,9);
                s[4*c+1]=gmul(a0,9)^gmul(a1,14)^gmul(a2,11)^gmul(a3,13);
                s[4*c+2]=gmul(a0,13)^gmul(a1,9)^gmul(a2,14)^gmul(a3,11);
                s[4*c+3]=gmul(a0,11)^gmul(a1,13)^gmul(a2,9)^gmul(a3,14);
            }
        }
        for(int r=1;r<4;r++){
            uint8_t row[4];
            for(int c=0;c<4;c++) row[c]=s[4*c+r];
            for(int c=0;c<4;c++) s[4*((c+r)%4)+r]=row[c];
        }
        const uint8_t* isb=invsbox();
        for(int i=0;i<16;i++) s[i]=isb[s[i]];
        addRound(s,0);
    }
};
static uint32_t Joaat(const char* s){
    uint32_t h=0;
    for(size_t i=0;s[i];i++){
        unsigned char c=(unsigned char)s[i];
        if(c>='A'&&c<='Z') c+=32;
        h+=c; h+=(h<<10); h^=(h>>6);
    }
    h+=(h<<3); h^=(h>>11); h+=(h<<15);
    return h;
}
static uint32_t RdBE(const std::vector<uint8_t>& b,size_t o){
    if(o+4>b.size()) return 0;
    return ((uint32_t)b[o]<<24)|((uint32_t)b[o+1]<<16)|((uint32_t)b[o+2]<<8)|b[o+3];
}
// синхронизация GTA V: счётчики коллекций из AES-расшифрованного сейва (Legacy и Enhanced)
static bool SyncGTA5(const std::vector<uint8_t>& b){
    if(b.size()<0x108+16+0x14) return false;
    static const uint8_t key[32]={
        0x46,0xed,0x8d,0x3f,0x94,0x35,0xe4,0xec,0x12,0x2c,0xb2,0xe2,0xaf,0x97,0xc5,0x7e,
        0x4c,0x5a,0x8c,0x30,0x92,0xc7,0x84,0x4e,0x11,0xc6,0x86,0xff,0x41,0xdf,0x41,0x0f};
    size_t bodyStart=0x108, bodyEnd=b.size()-0x14; // хвост CHKS 0x14
    size_t len=(bodyEnd-bodyStart)&~(size_t)15;
    std::vector<uint8_t> d(b.begin()+bodyStart,b.begin()+bodyStart+len);
    Aes256 aes(key);
    for(size_t i=0;i+16<=len;i+=16) aes.invCipher(&d[i]);
    if(d.size()<4||memcmp(&d[0],"PSIN",4)!=0){
        GTCLOG("gta5: расшифровка не дала PSIN");
        return false;
    }
    auto findStat=[&](const char* name,int maxVal)->long{
        uint32_t h=Joaat(name);
        for(size_t i=0;i+8<=d.size();i+=4){
            if(RdBE(d,i)==h){
                uint32_t v=RdBE(d,i+4);
                if(v<=(uint32_t)maxVal) return (long)v;
            }
        }
        return -1;
    };
    // ---- упакованные булевы флаги предметов: SP_PSTAT_BOOL0..8 ----
    // Блок лежит в SP-статах (сортированный массив записей 12 байт: u32 BE joaat + u64 BE value).
    // Бит g (0..575) хранится в слове SP_PSTAT_BOOL[g>>6], бит (g&63).
    static const uint32_t PSTAT_HASH[9]={0x0E3E6A20u,0x04F4578Cu,0xEABBA31Bu,0xDFA68CF1u,0x5559F856u,
                                         0x6BD02546u,0x3884BEACu,0x26361A0Fu,0xA4471647u};
    uint64_t pstat[9]={0}; bool havePstat=false;
    { // каждый хэш встречается ровно 1 раз; записи 12 байт [hash][u64 BE value],
      // лежат в SP-блоке перед PMAP, порядок хэшей произвольный
        int got=0; bool seen[9]={false};
        for(size_t i=0;i+12<=d.size()&&got<9;i++){
            uint32_t v=RdBE(d,i);
            for(int w=0;w<9;w++) if(v==PSTAT_HASH[w]&&!seen[w]){
                seen[w]=true; got++;
                pstat[w]=((uint64_t)RdBE(d,i+4)<<32)|RdBE(d,i+8);
                break;
            }
        }
        havePstat=(got==9);
    }
    GTCLOGF("gta5: SP_PSTAT_BOOL блок %s",havePstat?"найден":"НЕ найден");
    auto applyBits=[&](const wchar_t* cat,int g0,int n,long countVal)->bool{
        if(!havePstat) return false;
        Category* c=FindCatSub(cat); if(!c) return false;
        std::vector<int> fl((int)c->spots.size(),0); int cnt=0;
        for(int k=0;k<(int)c->spots.size()&&k<n;k++)
            if((pstat[(g0+k)>>6]>>((g0+k)&63))&1ULL){ fl[k]=1; cnt++; }
        if(countVal>=0&&cnt!=countVal) return false; // не сошлось со счётчиком
        auto&flv=FoundFlags(c->name);
        if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
        for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
        g.saveCounts[c->name]=cnt;
        return true;
    };
    struct B { const wchar_t* cat; int g0; int n; const char* cntStat; int max; };
    // g-биты: Letter 0-49, Spaceship 50-99, Epsilon 100-109, Nuclear 110-139, Submarine 140-169.
    // Биты 170-184 НЕ флаги Knife Flights (в честном сейве со счётчиком 0 все 15 бит стоят —
    // другая статистика). Under The Bridge тоже без флагов. Обе — только счётчики.
    B bits[]={ {L"Letter Scraps",   0, 50,"num_hidden_packages_0",50},
               {L"Spaceship Parts",50, 50,"num_hidden_packages_1",50},
               {L"Epsilon Tracts",100, 10,"num_hidden_packages_2",10},
               {L"Nuclear Waste", 110, 30,"num_hidden_packages_3",30},
               {L"Submarine",     140, 30,"num_hidden_packages_4",30} };
    int found=0;
    for(auto&s:bits){
        long v=findStat(s.cntStat,s.max);
        if(applyBits(s.cat,s.g0,s.n,v)){ found++; continue; }
        if(v>=0){ if(Category* c=FindCatSub(s.cat)){ MarkCountFromSave(*c,(int)v); found++; } }
    }
    struct C { const char* stat; const wchar_t* cat; int max; };
    C cntOnly[]={ {"sp_knife_flights_count",L"Knife Flights",15},
                  {"sp_under_the_bridge_count",L"Under The Bridge",50},
                  {"num_hidden_packages_5",L"Peyote",27},          // растения-оборотни
                  {"num_hidden_packages_6",L"Monkey Mosaic",50},   // мозаики обезьян
                  {"num_destroyed_signs_0",L"Realty Sign",15} };   // разбитые вывески (Sign Slasher)
    // Property: per-item bool-статы prop_bought_* (u8 по hash+4, 1=куплено; байты +1..+3 — мусор).
    // marker = индекс метки в data/gta5/gta5.txt секции Property (порядок сайта).
    static const struct{ const char* stat; int marker; } PROP[]={
        {"prop_bought_cscr", 0},  // Car Scrapyard
        {"prop_bought_cind", 1},  // Doppler Cinema
        {"prop_bought_taxi", 2},  // Downtown Cab Co.
        {"prop_bought_barho", 4}, // Hookies
        {"prop_bought_traf", 5},  // Los Santos Customs
        {"prop_bought_golf", 6},  // Los Santos Golf Club
        {"prop_bought_barpi",10}, // Pitchers
        {"prop_bought_weed", 12}, // Smoke on the Water
        {"prop_bought_soco", 13}, // Sonar Collections Dock
        {"prop_bought_cinm", 14}, // Ten Cent Theater
        {"prop_bought_barte",15}, // Tequi-La-La
        {"prop_bought_barhe",16}, // The Hen House
        {"prop_bought_cinv", 17}, // Tivoli Cinema
        {"prop_bought_towi", 18}, // Towing Impound
    };
    if(Category* pc=FindCatSub(L"Property")){
        auto&flv=FoundFlags(pc->name);
        if((int)flv.size()<(int)pc->spots.size()) flv.resize(pc->spots.size(),0);
        for(size_t i=0;i<flv.size();i++) flv[i]=0;
        int cnt=0;
        for(auto&p:PROP){
            uint32_t h=Joaat(p.stat);
            for(size_t i=0;i+8<=d.size();i+=4){
                if(RdBE(d,i)==h){
                    uint8_t v=d[i+4]; // bool: 1 байт, остальное мусор
                    if(v<=1){ flv[p.marker]=v; cnt+=v; }
                    break;
                }
            }
        }
        g.saveCounts[pc->name]=cnt;
        found++;
    }
    for(auto&s:cntOnly){
        long v=findStat(s.stat,s.max);
        if(v>=0){ if(Category* c=FindCatSub(s.cat)){ MarkCountFromSave(*c,(int)v); found++; } }
    }
    // Трюковые прыжки: USJS_COMPLETED_MASK — 64-битная маска (bit i = прыжок i выполнен)
    // плюс USJS_COMPLETED — счётчик для валидации
    if(Category* c=FindCatSub(L"Stunt")){
        uint32_t maskHash=Joaat("USJS_COMPLETED_MASK");
        uint32_t cntHash=Joaat("USJS_COMPLETED");
        long completed=-1;
        size_t maskOff=(size_t)-1;
        for(size_t i=0;i+12<=d.size();i+=4){
            if(RdBE(d,i)==cntHash){
                uint32_t v=RdBE(d,i+4);
                if(v<=50){ completed=(long)v; break; }
            }
        }
        for(size_t i=0;i+12<=d.size();i+=4){
            if(RdBE(d,i)==maskHash){
                uint64_t hi=RdBE(d,i+4), lo=RdBE(d,i+8);
                uint64_t mask=((uint64_t)hi<<32)|lo;
                int pc=0; uint64_t m=mask;
                while(m){ pc+=(int)(m&1); m>>=1; }
                if(pc<=50&&pc>=(completed>=0?completed:0)){
                    maskOff=i; break;
                }
            }
        }
        if(maskOff!=(size_t)-1&&completed>=0){
            uint64_t hi=RdBE(d,maskOff+4), lo=RdBE(d,maskOff+8);
            uint64_t mask=((uint64_t)hi<<32)|lo;
            std::vector<int> fl((int)c->spots.size(),0);
            int cnt=0;
            for(size_t i=0;i<c->spots.size()&&i<50;i++)
                if(mask&(1ULL<<i)){ fl[i]=1; cnt++; }
            if(cnt==completed){
                auto&flv=FoundFlags(c->name);
                if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
                for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
                g.saveCounts[c->name]=cnt;
                GTCLOGF("gta5: stunt mask ok, %d/50",cnt);
            } else {
                GTCLOGF("gta5: stunt mask=%d counter=%ld -> счётчик",cnt,completed);
                MarkCountFromSave(*c,(int)completed);
            }
        } else if(completed>=0){
            MarkCountFromSave(*c,(int)completed);
        }
    }
    GTCLOGF("gta5: статов найдено %d/5+stunt",found);
    return found>0;
}
// ---- DE-сейвы (.sav): zlib + таблица имён глобальных переменных main.scm ----
// puff-подобный inflate (zlib-поток: 2 байта заголовок + deflate + adler32)
struct ZBits { const uint8_t* d; size_t n, pos; uint32_t acc; int cnt; };
static int ZBitsGet(ZBits& z,int need){
    while(z.cnt<need){
        if(z.pos>=z.n) return -1;
        z.acc|=(uint32_t)z.d[z.pos++]<<z.cnt; z.cnt+=8;
    }
    int v=z.acc&((1<<need)-1); z.acc>>=need; z.cnt-=need; return v;
}
struct ZHuff { short cnt[16]; short sym[288]; int n; };
static int ZBuild(ZHuff& h,const uint8_t* len,int n){
    int i,left; short offs[16];
    for(i=0;i<16;i++) h.cnt[i]=0;
    for(i=0;i<n;i++) h.cnt[len[i]]++;
    if(h.cnt[0]==n) { h.n=-1; return 0; } // нет символов
    left=1; for(i=1;i<16;i++){ left<<=1; left-=h.cnt[i]; if(left<0) return -1; }
    offs[1]=0; for(i=1;i<15;i++) offs[i+1]=offs[i]+h.cnt[i];
    for(i=0;i<n;i++) if(len[i]) h.sym[offs[len[i]]++]=i;
    h.n=n; return left;
}
static int ZDecodeSym(ZBits& z,const ZHuff& h){
    int code=0,first=0,index=0;
    for(int len=1;len<16;len++){
        int b=ZBitsGet(z,1); if(b<0) return -1;
        code|=b;
        int count=h.cnt[len];
        if(code-first<count){ if(index+(code-first)>=h.n) return -1; return h.sym[index+(code-first)]; }
        index+=count; first+=count; first<<=1; code<<=1;
    }
    return -1;
}
static int ZInflateRaw(ZBits& z,std::vector<uint8_t>& out,size_t maxOut){
    static ZHuff lfix,dfix; static bool fixInit=false;
    if(!fixInit){
        uint8_t ll[288]; int i;
        for(i=0;i<144;i++) ll[i]=8; for(;i<256;i++) ll[i]=9; for(;i<280;i++) ll[i]=7; for(;i<288;i++) ll[i]=8;
        ZBuild(lfix,ll,288);
        for(i=0;i<30;i++) ll[i]=5; ZBuild(dfix,ll,30);
        fixInit=true;
    }
    static const short lb[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short eb[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short db[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short xb[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for(;;){
        int final=ZBitsGet(z,1), btype=ZBitsGet(z,2);
        if(final<0||btype<0) return -1;
        ZHuff lit,dst;
        const ZHuff* pl=&lfix; const ZHuff* pd=&dfix;
        if(btype==0){
            z.acc=0; z.cnt=0; // выровнять на байт
            if(z.pos+4>z.n) return -1;
            unsigned ln=z.d[z.pos]|(z.d[z.pos+1]<<8); z.pos+=4;
            if(z.pos+ln>z.n) return -1;
            for(unsigned k=0;k<ln;k++){ out.push_back(z.d[z.pos++]); if(out.size()>maxOut) return -2; }
        } else if(btype==1){
            // готовые фиксированные таблицы
        } else if(btype==2){
            int hlit=ZBitsGet(z,5)+257, hdist=ZBitsGet(z,5)+1, hclen=ZBitsGet(z,4)+4;
            if(hlit>286||hdist>30) return -1;
            uint8_t cl[19]={0},lens[320];
            static const short ord[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            for(int i=0;i<hclen;i++){ int v=ZBitsGet(z,3); if(v<0) return -1; cl[ord[i]]=(uint8_t)v; }
            ZHuff ch; if(ZBuild(ch,cl,19)<0) return -1;
            int n=0;
            while(n<hlit+hdist){
                int sym=ZDecodeSym(z,ch); if(sym<0) return -1;
                if(sym<16) lens[n++]=(uint8_t)sym;
                else{
                    int rep=0,val=0;
                    if(sym==16){ if(n==0) return -1; val=lens[n-1]; rep=3+ZBitsGet(z,2); }
                    else if(sym==17){ val=0; rep=3+ZBitsGet(z,3); }
                    else { val=0; rep=11+ZBitsGet(z,7); }
                    if(n+rep>hlit+hdist) return -1;
                    while(rep--) lens[n++]=(uint8_t)val;
                }
            }
            if(lens[256]==0) return -1;
            if(ZBuild(lit,lens,hlit)<0) return -1;
            if(ZBuild(dst,lens+hlit,hdist)<0) return -1;
            pl=&lit; pd=&dst;
        } else return -1;
        for(;;){
            int sym=ZDecodeSym(z,*pl); if(sym<0) return -1;
            if(sym<256){ out.push_back((uint8_t)sym); if(out.size()>maxOut) return -2; }
            else if(sym==256) break;
            else{
                sym-=257; if(sym>=29) return -1;
                int len=lb[sym]+ZBitsGet(z,eb[sym]);
                int dsym=ZDecodeSym(z,*pd); if(dsym<0) return -1;
                int dist=db[dsym]+ZBitsGet(z,xb[dsym]);
                if((size_t)dist>out.size()) return -1;
                for(int i=0;i<len;i++){ out.push_back(out[out.size()-dist]); if(out.size()>maxOut) return -2; }
            }
        }
        if(final) return 0;
    }
}
static int ZInflate(const std::vector<uint8_t>& src,size_t start,std::vector<uint8_t>& out,size_t maxOut){
    if(start+2>src.size()||src[start]!=0x78) return -1;
    int hdr=src[start]<<8|src[start+1];
    if(hdr%31!=0) return -1; // FCHECK
    ZBits z{&src[0],src.size(),start+2,0,0};
    int r=ZInflateRaw(z,out,maxOut);
    return r==0?(int)out.size():r;
}
// парсит таблицу имён глобальных переменных из zlib-записей внутри сейва DE
static bool ParseNameTableDE(const std::vector<uint8_t>& b,std::unordered_map<std::string,int>& names){
    bool any=false;
    for(size_t i=0;i+2<b.size();i++){
        if(b[i]!=0x78||(b[i+1]!=0x01&&b[i+1]!=0x9C&&b[i+1]!=0xDA&&b[i+1]!=0x5E)) continue;
        std::vector<uint8_t> out;
        if(ZInflate(b,i,out,4u*1024*1024)<0) continue;
        if(out.size()<32) continue;
        std::string s((const char*)&out[0],out.size());
        size_t gv=s.find("Globalvariables");
        if(gv==std::string::npos||gv>64) continue;
        size_t p=s.find('\n',gv);
        while(p!=std::string::npos&&p<s.size()){
            size_t e=s.find('\n',p+1); if(e==std::string::npos) e=s.size();
            std::string line=s.substr(p+1,e-p-1);
            if(!line.empty()&&line[0]!='/'){
                // MissionName,VariableName,ArrayCount,Index,Used[,Type]
                size_t c1=line.find(',');
                if(c1!=std::string::npos){
                    size_t c2=line.find(',',c1+1);
                    if(c2!=std::string::npos){
                        size_t c3=line.find(',',c2+1);
                        if(c3!=std::string::npos){
                            size_t c4=line.find(',',c3+1);
                            std::string name=line.substr(c1+1,c2-c1-1);
                            std::string idxs=line.substr(c3+1,(c4==std::string::npos?line.size():c4)-c3-1);
                            if(!name.empty()&&name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_@.")==std::string::npos)
                                names.emplace(name,atoi(idxs.c_str()));
                        }
                    }
                }
            }
            p=e;
        }
        any=true;
    }
    return any;
}
// база глобальных переменных: b"SCR\0" + 0x14 (или 0x15), валидируем по байтовым флагам 0/1
static size_t FindGlobalsBaseDE(const std::vector<uint8_t>& b,const std::unordered_map<std::string,int>& names,
                                const std::vector<std::string>& probes){
    size_t best=(size_t)-1; double bestScore=-1;
    for(size_t i=0;i+4<=b.size();i++){
        if(!(b[i]=='S'&&b[i+1]=='C'&&b[i+2]=='R'&&b[i+3]==0)) continue;
        for(int delta=0x14;delta<=0x15;delta++){
            size_t base=i+delta; double score=0; int tot=0;
            for(auto&p:probes){
                auto it=names.find(p); if(it==names.end()) continue;
                size_t off=base+it->second;
                if(off<b.size()&&(b[off]==0||b[off]==1)) score+=1;
                tot++;
            }
            if(tot&&score/tot>bestScore){ bestScore=score/tot; best=base; }
        }
    }
    return best;
}
static int DEVarIdx(const std::unordered_map<std::string,int>& names,const char* n){
    auto it=names.find(n); return it==names.end()?-1:it->second;
}
// ---- SA DE: именованные секции ----
static const char* SA_SECTIONS[]={"SIMPLE_VARIABLES","METADATA","SCRIPTS","POOLS","GARAGES","GAMELOGIC","PATHS","PICKUPS",
    "RESTART","RADAR","ZONES","GANGS","CARGENERATORS","PLAYERINFO","STATS","SETPIECES","STREAMING","PEDTYPE","TAGS",
    "IPLS","SHOPPING","GANGWARS","STUNTJUMPS","ENTRYEXIT","RADIO","USER_3DMARKERS","POSTEFFECTS","BRIEFS","SEARCHLIGHTS"};
struct SASections { size_t start[40],end[40]; int n=0; bool ok=false; };
static bool FindSASections(const std::vector<uint8_t>& b,SASections& s){
    size_t pos=0; s.n=0;
    for(const char* nm:SA_SECTIONS){
        uint32_t len=(uint32_t)strlen(nm)+1;
        uint8_t tag[80];
        tag[0]=(uint8_t)(len&0xFF); tag[1]=(uint8_t)((len>>8)&0xFF); tag[2]=(uint8_t)((len>>16)&0xFF); tag[3]=(uint8_t)((len>>24)&0xFF);
        memcpy(&tag[4],nm,len-1); tag[4+len-1]=0;
        size_t tlen=4+len;
        size_t i=b.size();
        for(size_t j=pos;j+tlen<=b.size();j++)
            if(!memcmp(&b[j],tag,tlen)){ i=j; break; }
        if(i>=b.size()){
            // секция отсутствует (новые патчи DE убирают SEARCHLIGHTS) — помечаем пустой, идём дальше
            s.start[s.n]=b.size(); s.n++;
            continue;
        }
        s.start[s.n]=i+4+len; s.n++; // начало данных секции
        pos=i+tlen;
    }
    for(int k=0;k<s.n;k++){
        size_t nextTag=(k+1<s.n)?(s.start[k+1]-4-strlen(SA_SECTIONS[k+1])-1):b.size();
        s.end[k]=nextTag;
    }
    s.ok=true;
    return true;
}
static bool SyncSA_DE(const std::vector<uint8_t>& b){
    SASections sec;
    if(!FindSASections(b,sec)) return false;
    int tagsI=-1,sjI=-1,scI=-1;
    for(int k=0;k<sec.n;k++){
        if(!strcmp(SA_SECTIONS[k],"TAGS")) tagsI=k;
        if(!strcmp(SA_SECTIONS[k],"STUNTJUMPS")) sjI=k;
        if(!strcmp(SA_SECTIONS[k],"SCRIPTS")) scI=k;
    }
    if(tagsI<0||sjI<0||scI<0) return false;
    size_t base=sec.start[scI]+12;
    std::unordered_map<std::string,int> names;
    ParseNameTableDE(b,names);
    GTCLOGF("sa_de: names=%u base=%u",names.size(),(unsigned)base);
    // теги: u32 count + байт на тег (закрашен >=229: у каждого тега свой максимум 229..255,
    // как в классике/редакторах; подтверждено 100% сейвом друга: закрашенный тег с байтом 229); порядок байтов = IPL-порядок
    uint32_t tcnt=Rd32(b,sec.start[tagsI]);
    if(Category* c=FindCatSub(L"Tag")){
        if(tcnt>=100&&tcnt<=200&&(sec.end[tagsI]-sec.start[tagsI])>=4+tcnt){
            std::vector<int> fl(100,0);
            for(int i=0;i<100;i++){
                int bi=SA_TAG_PERM[i];
                if(bi<0||bi>=(int)tcnt) continue;
                fl[i]=b[sec.start[tagsI]+4+bi]>=229?1:0;
            }
            MarkFlagsFromSave(*c,fl);
        }
    }
    // прыжки: u32 count + записи по 68 байт, done на +64; порядок записей не гарантирован —
    // сопоставляем по координатам (центр старт-бокса против метки в мировых единицах)
    uint32_t jcnt=Rd32(b,sec.start[sjI]);
    Category* sjc=FindCatSub(L"Stunt");
    if(sjc&&jcnt>=1&&jcnt<=120&&(sec.end[sjI]-sec.start[sjI])>=(size_t)(4+jcnt*68)){
        // в DE флагов done в записи нет (+64..67 — служебный float); счётчик — в STATS +0x1AC
        int si2=-1; for(int k=0;k<sec.n;k++) if(!strcmp(SA_SECTIONS[k],"STATS")) si2=k;
        long done=-1;
        if(si2>=0){
            long f=Rd32(b,sec.start[si2]+0x1A8), dn=Rd32(b,sec.start[si2]+0x1AC);
            if(dn>=0&&dn<=(long)jcnt&&f>=dn&&f<=(long)jcnt) done=dn;
        }
        if(done>=0){
            GTCLOGF("sa_de: jumps done=%ld (STATS)",done);
            MarkCountFromSave(*sjc,(int)done);
        } else {
            MarkCountFromSave(*sjc,0);
        }
    }
    // устрицы/подковы/снапшоты: живые пикапы в PICKUPS (620×32б, модель@+24, type@+28, позиция i16/8 @+16)
    int pkI=-1;
    for(int k=0;k<sec.n;k++) if(!strcmp(SA_SECTIONS[k],"PICKUPS")) pkI=k;
    struct PickKind { uint16_t model; const wchar_t* cat; const char* cntVar; };
    PickKind kinds[]={ {953,L"Oyster","RETURNED_OYSTERS"},{954,L"Horseshoe","RETURNED_SHOEHORSES"},{1253,L"Snapshot","RETURNED_SNAPSHOTS"} };
    if(pkI>=0){
        size_t ps=sec.start[pkI];
        for(auto&kd:kinds){
            Category* c=FindCatSub(kd.cat);
            if(!c) continue;
            std::vector<P2D> wl;
            for(int i=0;i<620;i++){
                size_t o=ps+(size_t)i*32;
                if(o+32>b.size()||o+32>sec.end[pkI]) break;
                if(Rd16(b,o+24)==kd.model&&b[o+28]!=0){
                    int16_t ix,iy; memcpy(&ix,&b[o+16],2); memcpy(&iy,&b[o+18],2);
                    wl.push_back({ix/8.0,iy/8.0});
                }
            }
            // авторитетный счётчик собранных
            long ctr=-1;
            {
                int idx=DEVarIdx(names,kd.cntVar);
                if(idx>=0&&base+idx+4<=b.size()) ctr=(long)Rd32(b,base+idx);
            }
            if(!wl.empty()){
                std::vector<P2D> dw;
                for(auto&s:c->spots) dw.push_back({SA_WX(s.x),SA_WY(s.y)});
                // считаем, сколько спотов имеет живой пикап рядом
                int matched=0;
                for(auto&s:dw){
                    for(auto&w:wl){
                        double dx=w.x-s.x,dy=w.y-s.y;
                        if(dx*dx+dy*dy<60.0*60.0){matched++;break;}
                    }
                }
                // принимаем пер-предметный вариант только если он сходится со счётчиком
                if(ctr>=0&&(int)c->spots.size()-matched==ctr){
                    MarkInverseLiveW(c,wl,60.0,dw);
                } else {
                    GTCLOGF("sa_de: %ls pickups matched=%d/%zu ctr=%d -> счётчик",(const wchar_t*)kd.cat,matched,c->spots.size(),(int)ctr);
                    if(ctr>=0) MarkCountFromSave(*c,(int)ctr);
                }
            } else if(ctr>=0) MarkCountFromSave(*c,(int)ctr);
        }
    }
    // счётчики как резерв (и для классики): RETURNED_*
    auto cntVar=[&](const char* nm,const wchar_t* cat){
        int idx=DEVarIdx(names,nm);
        if(idx<0) return;
        size_t off=base+idx;
        if(off+4>b.size()) return;
        if(Category* c=FindCatSub(cat)){
            auto it=g.saveCounts.find(c->name);
            if(it==g.saveCounts.end()) MarkCountFromSave(*c,(int)Rd32(b,off));
        }
    };
    cntVar("RETURNED_SNAPSHOTS",L"Snapshot");
    cntVar("RETURNED_SHOEHORSES",L"Horseshoe");
    cntVar("RETURNED_OYSTERS",L"Oyster");
    return true;
}
// ---- III DE / VC DE: глобальные переменные из таблицы имён ----
static bool SyncIVC_DE(const std::vector<uint8_t>& b,bool vc){
    std::unordered_map<std::string,int> names;
    if(!ParseNameTableDE(b,names)) { GTCLOG("ivc_de: no name table"); return false; }
    GTCLOGF("ivc_de: names=%u",names.size());
    std::vector<std::string> probes;
    if(vc) probes={"FLAG_USJ1_PASSED","FLAG_USJ2_PASSED","FLAG_USJ10_PASSED","SCPLAYER"};
    else   probes={"RAMPAGE_01_FLAG","RAMPAGE_10_FLAG","FLAG_USJ1_PASSED","FLAG_USJ10_PASSED"};
    size_t base=FindGlobalsBaseDE(b,names,probes);
    if(base==(size_t)-1||base>=b.size()) { GTCLOG("ivc_de: no globals base"); return false; }
    GTCLOGF("ivc_de: base=%u",(unsigned)base);
    auto flag=[&](const char* nm)->int{
        int idx=DEVarIdx(names,nm); if(idx<0) return -1;
        size_t off=base+idx; return off<b.size()?b[off]:-1;
    };
    auto cnt32=[&](const char* nm)->long{
        int idx=DEVarIdx(names,nm); if(idx<0) return -1;
        size_t off=base+idx; return off+4<=b.size()?(long)Rd32(b,off):-1;
    };
    // III: блок статистики — по константам (154 прогресс, 20 прыжков, 20 rampages, 73 миссии)
    size_t stats=(size_t)-1;
    if(!vc){
        uint8_t needle[4]={154,0,0,0};
        for(size_t i=0x74;i+0xA0<b.size();i++){
            if(memcmp(&b[i],needle,4)) continue;
            if(Rd32(b,i+0x40)==20&&Rd32(b,i+0x98)==20&&Rd32(b,i+0x9C)==73){ stats=i-0x74; break; }
        }
    }
    if(Category* c=FindCatSub(L"Package")){
        if(vc){
            long n=cnt32("TAXI_PASSED");
            if(n>=0&&n<=100) MarkCountFromSave(*c,(int)n);
        } else {
            long n=cnt32("NUMBER_OF_PACKAGES_COLLECTED");
            // пер-предметно: собранный пакет = его пикап освобождён (type=0); несобранные живы
            // (type=5, модель 1321) — «собрано» = спот без живого пикапа в радиусе 60
            size_t pk=(size_t)-1;
            for(size_t i=0;i+9500<b.size();i++)
                if(Rd32(b,i+9492)==2608&&Rd32(b,i+9496)==49){ pk=i; break; }
            if(pk!=(size_t)-1){
                std::vector<P2D> wl;
                for(int e=0;e<336;e++){
                    size_t p=pk+(size_t)e*0x1C;
                    if(p+0x1C>b.size()) break;
                    if(b[p]==5&&Rd16(b,p+0xC)==1321){
                        float x,y; memcpy(&x,&b[p+0x10],4); memcpy(&y,&b[p+0x14],4);
                        if(fabsf(x)<3000&&fabsf(y)<3000) wl.push_back({x,y}); // мировые координаты
                    }
                }
                if(!wl.empty()){
                    std::vector<P2D> dw;
                    for(auto&s:c->spots) dw.push_back({G3_WX(s.x),G3_WY(s.y)});
                    MarkInverseLiveW(c,wl,60.0,dw); // пикапы уже в мировых координатах
                    GTCLOGF("iii_de: pk live=%zu spots=%zu",wl.size(),dw.size());
                    // кросс-чек со счётчиком сценария: при расхождении верим счётчику
                    auto it=g.saveCounts.find(c->name);
                    if(n>=0&&it!=g.saveCounts.end()&&it->second!=(int)n){
                        GTCLOGF("iii_de: packages pickups=%d counter=%d -> счётчик",it->second,(int)n);
                        MarkCountFromSave(*c,(int)n);
                    }
                } else if(n>=0) MarkCountFromSave(*c,(int)n);
            } else if(n>=0) MarkCountFromSave(*c,(int)n);
        }
    }
    if(Category* c=FindCatSub(L"Rampage")){
        if(vc){
            int idx=DEVarIdx(names,"S_PUNTER_2");
        if(idx>=0&&base+idx<b.size()){ int v=b[base+idx]; if(v>=0&&v<=35) MarkCountFromSave(*c,v); // счётчик в младшем байте, кап 35
        }
        } else {
            std::vector<int> fl; char nm[32];
            for(int i=1;i<=20;i++){ sprintf(nm,"RAMPAGE_%02d_FLAG",i); int v=flag(nm); if(v<0){ fl.clear(); break; } fl.push_back(v?1:0); }
            // валидация по статистике: сумма флагов должна совпадать с счётчиком
            int sum=0; for(int f:fl) sum+=f;
            if(stats!=(size_t)-1){
                int ctr=(int)Rd32(b,stats+0x108);
                if(sum!=ctr){ GTCLOGF("iii_de: rampage flags=%d stats=%d -> только счётчик",sum,ctr); fl.clear(); }
                if(fl.empty()) MarkCountFromSave(*c,ctr);
            }
            if(!fl.empty()) MarkFlagsFromSave(*c,fl);
        }
    }
    if(Category* c=FindCatSub(L"Stunt")){
        int maxJ=vc?36:20; std::vector<int> fl; char nm[32];
        for(int i=1;i<=maxJ;i++){ sprintf(nm,"FLAG_USJ%d_PASSED",i); int v=flag(nm); if(v<0){ fl.clear(); break; } fl.push_back(v?1:0); }
        int sum=0; for(int f:fl) sum+=f;
        if(vc){
            long ctr=cnt32("TOTAL_COMPLETED_USJ");
            if(sum<0||(ctr>=0&&sum!=(int)ctr)){ GTCLOGF("vc_de: usj flags=%d ctr=%d -> только счётчик",sum,(int)ctr); fl.clear(); if(ctr>=0) MarkCountFromSave(*c,(int)ctr); }
        } else if(stats!=(size_t)-1){
            int ctr=(int)Rd32(b,stats+0xB0);
            if(sum!=ctr){ GTCLOGF("iii_de: usj flags=%d stats=%d -> только счётчик",sum,ctr); fl.clear(); }
            if(fl.empty()) MarkCountFromSave(*c,ctr);
        }
        if(!fl.empty()) MarkFlagsFromSave(*c,fl);
    }
    if(vc){ if(Category* c=FindCatSub(L"Rober")){
        int idx=DEVarIdx(names,"ICEBUY_BLIP");
        if(idx>=0&&base+idx<b.size()){ int v=b[base+idx]; if(v>=0&&v<=15) MarkCountFromSave(*c,v); // кап 15
        }
    } }
    return true;
}

static void SyncIVCPerItem(const std::vector<uint8_t>& b,bool vc,bool isDE,const std::vector<long>& statsVals);
static void SyncFromSave(){
    g.saveCounts.clear();
    if(g.savePath.empty()||g.cats.empty()||g.rt==nullptr) return;
    FILE* f=_wfopen(g.savePath.c_str(),L"rb");
    if(!f) return;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    std::vector<uint8_t> b((size_t)tmax(n,0L));
    if(!b.empty()) fread(b.data(),1,b.size(),f);
    fclose(f);
    GameDef& gd=CurGame();
    gdIsVC=(gd.id==L"vc_de");
    bool ok=false;
    if(gd.id==L"gta4"){
        ok=SyncGTA4(b);
    } else if(gd.id==L"gta5"){
        ok=SyncGTA5(b);
    }
    if(gd.id==L"sa_de"){
        if(b.size()>5&&!memcmp(&b[0],"BLOCK",5)) ok=SyncClassicSA(b);
        else ok=SyncSA_DE(b); // .sav Definitive: именованные секции
    } else if(gd.id!=L"gta4"&&gd.id!=L"gta5"){
        IVCBlocks bl;
        if(WalkBlocksIVC(b,bl)){
            // классика: [size][data][size] без 00FF00FF-заголовка
            ok=SyncIVCPackages(b,bl,gdIsVC?18:16,gdIsVC?19:17,gdIsVC?8:7,gdIsVC?0x34:0x1C,gdIsVC?0:0x10,gdIsVC?0x2F:0x01,gdIsVC?0x22:0x0C,gdIsVC?100:20);
            SyncIVCPerItem(b,gdIsVC,false,{-1,-1,-1,-1,-1}); // per-item через script globals, статы неизвестны
        } else {
            // Definitive Edition (.sav): заголовок 00FF00FF, глобальные переменные main.scm
            ok=SyncIVC_DE(b,gdIsVC);
        }
    }
    // ok=false не затирает per-item результаты (классика III/VC: счётчики пакетов могут
    // не распознаться, но per-item по глобалам уже заполнил saveCounts)
    for(auto&kv:g.saveCounts){
        char cb[128]={0};
        WideCharToMultiByte(CP_UTF8,0,kv.first.c_str(),-1,cb,127,nullptr,nullptr);
        GTCLOGF("sync %s = %d",cb,kv.second);
    }
    if(!g.saveCounts.size()) GTCLOG("sync: nothing parsed");
}

static void ParseDataFile(){
    g.cats.clear();
    GameDef& gd=CurGame();
    std::ifstream f(std::filesystem::path(gd.dataDir+L"\\"+gd.dataFile));
    if(!f) return;
    std::string line;
    while(std::getline(f,line)){
        while(!line.empty()&&(line.back()=='\r'||line.back()=='\n'||line.back()==' '||line.back()=='\t')) line.pop_back();
        size_t a=0; while(a<line.size()&&(line[a]==' '||line[a]=='\t'))a++;
        std::string s=line.substr(a);
        if(s.empty()) continue;
        if(isdigit((unsigned char)s[0])||s[0]=='-'||s[0]=='+'){
            float x=0,y=0;
            if(sscanf(s.c_str(),"%f, %f",&x,&y)!=2) sscanf(s.c_str(),"%f , %f",&x,&y);
            if(sscanf(s.c_str(),"%f, %f",&x,&y)!=2) sscanf(s.c_str(),"%f ,%f",&x,&y);
            if(sscanf(s.c_str(),"%f, %f",&x,&y)!=2) sscanf(s.c_str(),"%f %f",&x,&y);
            if(!g.cats.empty()) g.cats.back().spots.push_back({x,y,(int)g.cats.back().spots.size()+1});
        } else {
            Category c; c.name=Utf8ToWide(s); g.cats.push_back(c);
        }
    }
}

static const wchar_t* CatInfo(const std::wstring& n){
    if(n.find(L"Tag")!=std::wstring::npos) return L"Бандитские теги. Опрыскайте все граффити спрей-баллончиком (100 шт.). Награда: оружие у дома Джонсонов + уважение.";
    if(n.find(L"Snapshot")!=std::wstring::npos) return L"Снимки. Сфотографируйте достопримечательности Сан-Фиерро (50 шт.). Награда: деньги и оружие на верфи.";
    if(n.find(L"Horseshoe")!=std::wstring::npos) return L"Подковы Лас-Вентураса (50 шт.). Награда: M4, MP5, Combat Shotgun и $100 000.";
    if(n.find(L"Oyster")!=std::wstring::npos) return L"Устрицы под водой (50 шт.). Награда: запас кислорода и $100 000.";
    if(n.find(L"Stunt")!=std::wstring::npos) return L"Уникальные трюковые прыжки. Выполните все прыжки на транспорте.";
    if(n.find(L"Package")!=std::wstring::npos) return L"Скрытые пакеты. За каждые 10 собранных — оружие появляется в убежищах.";
    if(n.find(L"Rampage")!=std::wstring::npos) return L"Задания Rampage. Пройдите все «бесчинства» за отведённое время.";
    if(n.find(L"Robber")!=std::wstring::npos||n.find(L"Rober")!=std::wstring::npos) return L"Ограбления магазинов. Войдите в магазин, укажите оружие на продавца и заберите кассу.";
    if(n.find(L"Pigeon")!=std::wstring::npos) return L"Летающие крысы (200 шт.). Стреляйте по голубям по всему Либерти-Сити. Награда: вертолёт Annihilator у каждой из ваших safe house.";
    if(n.find(L"Seagull")!=std::wstring::npos&&n.find(L"TBOGT")!=std::wstring::npos) return L"Чайки The Ballad of Gay Tony (50 шт.). Стреляйте по чайкам, вращающимся над городом. Награда: bonuses TBOGT.";
    if(n.find(L"Seagull")!=std::wstring::npos) return L"Чайки The Lost and Damned (50 шт.). Стреляйте по чайкам по всему Либерти-Сити. Награда: бонусы TLAD.";
    if(n.find(L"Letter Scrap")!=std::wstring::npos) return L"Обрывки писем (50 шт.). Соберите все обрывки, чтобы собрать письма Лесли — доказательство для шантажа Деборы.";
    if(n.find(L"Spaceship")!=std::wstring::npos) return L"Части космического корабля (50 шт.). Соберите их за Омегу в Sahuarito. Награда: космический корабль и Space Docker.";
    if(n.find(L"Submarine")!=std::wstring::npos) return L"Части субмарины (30 шт.). Найдите обломки подлодки под водой за Омегу. Награда: подводный аппарат.";
    if(n.find(L"Under The Bridge")!=std::wstring::npos) return L"Пролёты под мостами (50 шт.). Пролетите под мостами на самолёте/вертолёте — задание «Close Shave».";
    if(n.find(L"Knife Flight")!=std::wstring::npos) return L"Полёты «ножи» (15 шт.). Выполните фигуры высшего пилотажа между зданиями Лос-Сантоса.";
    if(n.find(L"Epsilon")!=std::wstring::npos) return L"Трактаты Киффлера (10 шт.). Брошюры культа Epsilonism — часть задания «Seeking the Truth».";
    if(n.find(L"Nuclear Waste")!=std::wstring::npos) return L"Бочки с ядерными отходами (30 шт.). Поднимите со дна океана после миссии «The Wrap Up». Награда: Waste Dumper.";
    if(n.find(L"Peyote")!=std::wstring::npos) return L"Растения пейот (27 шт.). Съешьте, чтобы превратиться в животное. Автоскрытие по счётчику.";
    if(n.find(L"Monkey Mosaic")!=std::wstring::npos) return L"Мозаики обезьян (50 шт.). Сфотографируйте 50 частей мозаик для фото-челленджа Los Santos. Автоскрытие по счётчику.";
    if(n.find(L"Realty Sign")!=std::wstring::npos) return L"Вывески Lenny Avery (15 шт.). Разбейте таблички недвижимости — задание «Sign Slasher». Автоскрытие по счётчику.";
    if(n.find(L"Oddball")!=std::wstring::npos) return L"Чудики Лос-Сантоса (12 шт.). Взаимодействуйте со странными персонажами для достижения. Прогресс игрой не сохраняется.";
    if(n.find(L"Property")!=std::wstring::npos) return L"Недвижимость (21 объект). Купленная скрывается автоматически (14 объектов со статами: кабкомпания, бары, кинотеатры и т.д.); ангар, вертолётная площадка и гаражи статами не учтены.";
    return L"Коллекционный предмет для 100% прохождения.";
}

// ---------------------------------------------------------------- state
static void SaveState(){
    std::wstring dir=AppDataDir()+L"\\GrandTheftCompletionist";
    CreateDirectoryW(dir.c_str(),nullptr);
    std::wstring j=L"{";
    j+=L"\"found\":{";
    bool first=true;
    for(auto&kv:g.found){
        if(!first)j+=L","; first=false;
        j+=L"\""+JEsc(kv.first)+L"\":[";
        for(size_t i=0;i<kv.second.size();i++){ if(i)j+=L","; j+=std::to_wstring(kv.second[i]); }
        j+=L"]";
    }
    j+=L"},\"hidden\":{";
    bool hf=true;
    for(auto&kv:g.hiddenCats){
        if(kv.second==1){ if(!hf)j+=L","; hf=false; j+=L"\""+JEsc(kv.first)+L"\":1"; }
    }
    j+=L"}";
    j+=L",\"cal\":{";
    first=true;
    for(auto&gd:g.games){
        Cal& cl=g.cals[gd.id];
        if(!first)j+=L","; first=false;
        wchar_t b[192]; swprintf(b,192,L"\"%s\":{\"dx\":%.2f,\"dy\":%.2f,\"scale\":%.5f,\"fx\":%d,\"fy\":%d}",
            JEsc(gd.id).c_str(),cl.dx,cl.dy,cl.scale,cl.fx?1:0,cl.fy?1:0);
        j+=b;
    }
    j+=L"},\"spotoff\":{";
    first=true;
    for(auto&kv:g.spotOffs){
        if(!first)j+=L","; first=false;
        wchar_t b[96]; swprintf(b,96,L"\"%s\":[%.2f,%.2f]",JEsc(kv.first).c_str(),kv.second.x,kv.second.y);
        j+=b;
    }
    j+=L"},\"mcal\":{";
    first=true;
    for(auto&gd:g.games){
        Cal& cl=g.mcals[gd.id];
        if(!first)j+=L","; first=false;
        wchar_t b[192]; swprintf(b,192,L"\"%s\":{\"dx\":%.2f,\"dy\":%.2f,\"scale\":%.5f,\"fx\":%d,\"fy\":%d}",
            JEsc(gd.id).c_str(),cl.dx,cl.dy,cl.scale,cl.fx?1:0,cl.fy?1:0);
        j+=b;
    }
    j+=L"}";
    wchar_t ab[160]; swprintf(ab,160,L",\"activeGame\":%d,\"hotkeyVK\":%u,\"definitiveEdition\":%d}",g.activeGame,g.hotkeyVK,g.definitiveEdition?1:0);
    j+=ab;
    std::ofstream f(std::filesystem::path(dir+L"\\state.json"),std::ios::binary);
    f<<WideToUtf8(j);
}
static void LoadState(){
    std::ifstream f(std::filesystem::path(AppDataDir()+L"\\GrandTheftCompletionist\\state.json"),std::ios::binary);
    if(!f) return;
    std::stringstream ss; ss<<f.rdbuf();
    std::wstring s=Utf8ToWide(ss.str());
    size_t i=0; JValue v=JParse(s,i);
    if(const JValue* fd=v.get(L"found"))
        for(auto&kv:fd->obj){ std::vector<int> fl; for(auto&e:kv.second.arr) fl.push_back(e.num>0.5?1:0); g.found[kv.first]=fl; }
    if(const JValue* ag=v.get(L"activeGame")){ int a=(int)ag->num; if(a>=0&&a<(int)g.games.size()) g.activeGame=a; }
    if(const JValue* de=v.get(L"definitiveEdition")) g.definitiveEdition=(de->num>0.5);
    if(const JValue* hd=v.get(L"hidden")) for(auto&kv:hd->obj) g.hiddenCats[kv.first]=(int)kv.second.num;
    if(const JValue* cl=v.get(L"cal"))
        for(auto&kv:cl->obj){
            Cal c;
            if(auto*p=kv.second.get(L"dx"))c.dx=p->num;
            if(auto*p=kv.second.get(L"dy"))c.dy=p->num;
            if(auto*p=kv.second.get(L"scale"))c.scale=p->num;
            if(auto*p=kv.second.get(L"fx"))c.fx=p->num>0.5;
            if(auto*p=kv.second.get(L"fy"))c.fy=p->num>0.5;
            g.cals[kv.first]=c;
        }
    if(const JValue* mc=v.get(L"mcal"))
        for(auto&kv:mc->obj){
            Cal c;
            if(auto*p=kv.second.get(L"dx"))c.dx=p->num;
            if(auto*p=kv.second.get(L"dy"))c.dy=p->num;
            if(auto*p=kv.second.get(L"scale"))c.scale=p->num;
            if(auto*p=kv.second.get(L"fx"))c.fx=p->num>0.5;
            if(auto*p=kv.second.get(L"fy"))c.fy=p->num>0.5;
            g.mcals[kv.first]=c;
        }
    if(const JValue* so=v.get(L"spotoff"))
        for(auto&kv:so->obj)
            if(kv.second.arr.size()>=2)
                g.spotOffs[kv.first]={ (float)kv.second.arr[0].num, (float)kv.second.arr[1].num };
    if(const JValue* hk=v.get(L"hotkeyVK")){ UINT k=(UINT)hk->num; if(k>0&&k<256&&k!=VK_LBUTTON&&k!=VK_RBUTTON) g.hotkeyVK=k; }
}

// ---------------------------------------------------------------- bitmaps
static void LoadMapBitmap(){
    GameDef& gd=CurGame();
    if(g.mapBmpFor==gd.mapFile&&g.mapBmp) return;
    SafeRelease(&g.mapBmp); SafeRelease(&g.mapBmp2); SafeRelease(&g.mapBmp4);
    std::wstring path=ExeDir()+L"\\assets\\"+gd.mapFile;
    if(!FileExists(path)) path=ExeDir()+L"\\..\\assets\\"+gd.mapFile;
    IWICBitmapDecoder* dec=nullptr;
    if(SUCCEEDED(g.wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec))){
        IWICBitmapFrameDecode* fr=nullptr;
        if(SUCCEEDED(dec->GetFrame(0,&fr))){
            IWICFormatConverter* cv=nullptr;
            if(SUCCEEDED(g.wic->CreateFormatConverter(&cv))){
                cv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeMedianCut);
                UINT w=0,h=0; fr->GetSize(&w,&h);
                g.rt->CreateBitmapFromWicBitmap(cv,nullptr,&g.mapBmp);
                g.mapW=w; g.mapH=h;
                // LOD-мипмапы: быстрая отрисовка при панораме/зуме (меньше пикселей = нет лагов)
                if(w>8&&h>8){
                    IWICBitmapScaler* sc2=nullptr;
                    if(SUCCEEDED(g.wic->CreateBitmapScaler(&sc2))){
                        if(SUCCEEDED(sc2->Initialize(cv,w/2,h/2,WICBitmapInterpolationModeFant)))
                            g.rt->CreateBitmapFromWicBitmap(sc2,nullptr,&g.mapBmp2);
                        SafeRelease(&sc2);
                    }
                    IWICBitmapScaler* sc4=nullptr;
                    if(SUCCEEDED(g.wic->CreateBitmapScaler(&sc4))){
                        if(SUCCEEDED(sc4->Initialize(cv,w/4,h/4,WICBitmapInterpolationModeFant)))
                            g.rt->CreateBitmapFromWicBitmap(sc4,nullptr,&g.mapBmp4);
                        SafeRelease(&sc4);
                    }
                }
                SafeRelease(&cv);
            }
            SafeRelease(&fr);
        }
        SafeRelease(&dec);
    }
    g.mapBmpFor=gd.mapFile;
    GTCLOGF("map bitmap loaded %ux%u (%ls)",(unsigned)g.mapW,(unsigned)g.mapH,gd.mapFile.c_str());
}
// Декодирует PNG-иконку один раз и кэширует источник WIC: BuildIconLayer
// пересобирается при зуме — без этого кэша каждая перестройка перечитывала
// сотни PNG с диска и карта фризила прямо посреди жеста зума.
static IWICBitmapSource* GetIconWic(const std::wstring& path){
    auto it=g.iconWicCache.find(path);
    if(it!=g.iconWicCache.end()) return it->second;
    IWICBitmapSource* src=nullptr;
    if(FileExists(path)){
        IWICBitmapDecoder* dec=nullptr;
        if(SUCCEEDED(g.wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&dec))){
            IWICBitmapFrameDecode* fr=nullptr;
            if(SUCCEEDED(dec->GetFrame(0,&fr))){
                IWICFormatConverter* cv=nullptr;
                if(SUCCEEDED(g.wic->CreateFormatConverter(&cv))){
                    if(SUCCEEDED(cv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeMedianCut))){
                        src=cv; cv->AddRef(); // конвертер — валидный IWICBitmapSource
                    }
                    SafeRelease(&cv);
                }
                SafeRelease(&fr);
            }
            SafeRelease(&dec);
        }
    }
    g.iconWicCache[path]=src;
    return src;
}
static ID2D1Bitmap* LoadIconBitmap(const std::wstring& cat){
    auto it=g.iconCache.find(cat);
    if(it!=g.iconCache.end()) return it->second;
    std::wstring path=CurGame().dataDir+L"\\"+cat+L".png";
    ID2D1Bitmap* bmp=nullptr;
    if(IWICBitmapSource* src=GetIconWic(path)) g.rt->CreateBitmapFromWicBitmap(src,nullptr,&bmp);
    g.iconCache[cat]=bmp;
    return bmp;
}

// ---------------------------------------------------------------- map view (pan/zoom, как в SA_Map_Overlay)
static D2D1_RECT_F MapRect(float W,float H);
static void Render();
static float viewScale=1.0f, viewTx=0.0f, viewTy=0.0f;
static bool mapDragging=false, mapZooming=false;
static int mapDragX=0, mapDragY=0;
static float mapDragTx=0, mapDragTy=0;
static float mapZoomTarget=1.0f, mapZoomAnchorX=0, mapZoomAnchorY=0;
static float mapZoomBaseTx=0, mapZoomBaseTy=0, mapZoomBaseScale=1.0f;
static D2D1_RECT_F ViewMapRect(float W,float H){
    D2D1_RECT_F r=MapRect(W,H);
    float cx=(r.left+r.right)*0.5f, cy=(r.top+r.bottom)*0.5f;
    return D2D1::RectF(cx+(r.left-cx)*viewScale+viewTx, cy+(r.top-cy)*viewScale+viewTy,
                       cx+(r.right-cx)*viewScale+viewTx, cy+(r.bottom-cy)*viewScale+viewTy);
}
static void ProcessMapZoom(){
    if(!mapZooming) return;
    float ns=viewScale+(mapZoomTarget-viewScale)*0.28f;
    if(fabsf(ns-mapZoomTarget)<0.0005f*mapZoomTarget){ ns=mapZoomTarget; mapZooming=false; }
    RECT rc; GetClientRect(g.hwnd,&rc);
    float cx=(float)rc.right*0.5f, cy=(float)rc.bottom*0.5f;
    viewTx=(mapZoomAnchorX-cx)-((mapZoomAnchorX-cx)-mapZoomBaseTx)*ns/mapZoomBaseScale;
    viewTy=(mapZoomAnchorY-cy)-((mapZoomAnchorY-cy)-mapZoomBaseTy)*ns/mapZoomBaseScale;
    viewScale=ns; Render();
}
// ---------------------------------------------------------------- geometry
static D2D1_RECT_F MapRect(float W,float H){
    if(!g.mapBmp) return {MENU_PANEL_W+20,20,W-20,H-20};
    float availW=W-MENU_PANEL_W-40, availH=H-40;
    double base=(double)min(availW,(float)(availH*(double)g.mapW/g.mapH));
    double sc=base/(double)g.mapW*0.62; // статичный масштаб (тот же, что был при zoom=1)
    float dw=(float)(g.mapW*sc), dh=(float)(g.mapH*sc);
    return { W/2.0f-dw/2.0f, H/2.0f-dh/2.0f, W/2.0f+dw/2.0f, H/2.0f+dh/2.0f };
}
static D2D1_POINT_2F WorldToScreen(const D2D1_RECT_F& mr,double wx,double wy){
    GameDef& gd=CurGame();
    double u,v;
    if(gd.swapXY){ u=wy*gd.sx+gd.ox; v=wx*gd.sy+gd.oy; }
    else         { u=wx*gd.sx+gd.ox; v=wy*gd.sy+gd.oy; }
    // пользовательская калибровка меток (режим «Калибровка»): сдвиг/масштаб/отражение
    Cal& cl=g.cals[gd.id];
    if(cl.fx) u=(double)g.mapW-u;
    if(cl.fy) v=(double)g.mapH-v;
    if(cl.scale!=1.0){
        u=(u-(double)g.mapW/2.0)*cl.scale+(double)g.mapW/2.0;
        v=(v-(double)g.mapH/2.0)*cl.scale+(double)g.mapH/2.0;
    }
    u+=cl.dx; v+=cl.dy;
    double sc=(mr.right-mr.left)/(double)g.mapW;
    return { (float)(mr.left+u*sc), (float)(mr.top+v*sc) };
}
static std::wstring SpotKey(const std::wstring& cat,int idx){
    std::wstring k=CurGame().id; k+=L"|"; k+=cat; k+=L"|"; k+=std::to_wstring(idx); return k;
}
// позиция метки на экране с учётом её индивидуального сдвига (режим калибровки)
static D2D1_POINT_2F SpotPos(const D2D1_RECT_F& mr,const std::wstring& cat,int idx,double wx,double wy){
    D2D1_POINT_2F p=WorldToScreen(mr,wx,wy);
    auto it=g.spotOffs.find(SpotKey(cat,idx));
    if(it!=g.spotOffs.end()){
        double sc=(mr.right-mr.left)/(double)g.mapW;
        p.x+=(float)(it->second.x*sc); p.y+=(float)(it->second.y*sc);
    }
    return p;
}
// Карта+иконки в произвольный RT (экран или кэш-канвас с полями)
static D2D1_RECT_F MapDrawRect(const D2D1_RECT_F& mr);
static void DrawMapAndIcons(ID2D1RenderTarget* t,float W,float H){
    if(!g.mapBmp) return;
    ID2D1SolidColorBrush* br=nullptr;
    t->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),&br);
    GameDef& gd=CurGame();
    D2D1_RECT_F mr=ViewMapRect(W,H);
    D2D1_RECT_F mrd=MapDrawRect(mr);
    t->PushAxisAlignedClip(mrd,D2D1_ANTIALIAS_MODE_ALIASED);
    br->SetColor(D2D1::ColorF(0.36f,0.43f,0.49f,1.0f)); // фон-подложка (для радарных тайлов GTA IV; у DE карт он не виден)
    t->FillRectangle(mrd,br);
    ID2D1Bitmap* mb=g.mapBmp;
    float drawW=mr.right-mr.left;
    // LOD: mip только когда рисуем явно меньше его размера (иначе пикселизация при жестах)
    if(g.mapBmp4&&drawW<g.mapW*0.25f) mb=g.mapBmp4;
    else if(g.mapBmp2&&drawW<g.mapW*0.5f) mb=g.mapBmp2;
    Cal& mc=g.mcals[gd.id];
    if(mc.fx||mc.fy){
        float mcx=(mrd.left+mrd.right)/2, mcy=(mrd.top+mrd.bottom)/2;
        t->SetTransform(D2D1::Matrix3x2F::Translation(D2D1::SizeF(mcx,mcy))
            *D2D1::Matrix3x2F::Scale(D2D1::SizeF(mc.fx?-1.0f:1.0f,mc.fy?-1.0f:1.0f),D2D1::Point2F(0,0))
            *D2D1::Matrix3x2F::Translation(D2D1::SizeF(-mcx,-mcy)));
    }
    t->DrawBitmap(mb,mrd,1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if(mc.fx||mc.fy) t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->PopAxisAlignedClip();
    t->PushAxisAlignedClip(mr,D2D1_ANTIALIAS_MODE_ALIASED);
    // иконки: всегда напрямую в экранных пикселях (карта статична — скорости хватает с запасом)
    for(size_t ci=0;ci<g.cats.size();ci++){
        auto&c=g.cats[ci];
        if(g.hiddenCats[c.name]==1) continue;
        ID2D1Bitmap* ib=LoadIconBitmap(c.name);
        auto&fl=FoundFlags(c.name);
        for(size_t si=0;si<c.spots.size();si++){
            bool f=si<fl.size()&&fl[si]==1;
            if(f&&!g.showFound) continue;
            D2D1_POINT_2F p=SpotPos(mr,c.name,(int)si,c.spots[si].x,c.spots[si].y);
            if(p.x<mr.left-10||p.x>mr.right+10||p.y<mr.top-10||p.y>mr.bottom+10) continue;
            if(ib){
                // пин как на сайте: правильные пропорции, остриё в точке координаты, тень
                D2D1_SIZE_F isz=ib->GetSize();
                float h=18.0f, w=h*isz.width/isz.height;
                br->SetColor(D2D1::ColorF(0,0,0,0.45f));
                t->FillEllipse(D2D1::Ellipse({p.x,p.y},5.0f,2.0f),br);
                t->DrawBitmap(ib,D2D1::RectF(p.x-w/2,p.y-h,p.x+w/2,p.y),f?0.30f:1.0f,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                float sz=14.0f;
                br->SetColor(D2D1::ColorF(0,0,0,0.45f));
                t->FillEllipse(D2D1::Ellipse({p.x,p.y},5.0f,2.0f),br);
                br->SetColor(f?D2D1::ColorF(0.4f,0.95f,0.5f,0.95f):D2D1::ColorF(1,0.85f,0.15f,0.95f));
                t->FillEllipse(D2D1::Ellipse({p.x,p.y-6.0f},7.0f,7.0f),br);
            }
        }
    }
    t->PopAxisAlignedClip();
    br->Release();
}

static bool InRect(const D2D1_RECT_F&r,float x,float y){ return x>=r.left&&x<=r.right&&y>=r.top&&y<=r.bottom; }

// Карта статична: перетаскивания и зума нет — фиксированный масштаб (zoom=1)
// и центр в окне. Никаких View (cx/cy/zoom) не существует.
// прямоугольник, в котором реально рисуется карта (с учётом пользовательской калибровки карты)
static D2D1_RECT_F MapDrawRect(const D2D1_RECT_F& mr){
    Cal& mc=g.mcals[CurGame().id];
    if(mc.scale==1.0&&mc.dx==0&&mc.dy==0) return mr;
    float mcx=(mr.left+mr.right)/2, mcy=(mr.top+mr.bottom)/2;
    float dw=(mr.right-mr.left)*(float)mc.scale, dh=(mr.bottom-mr.top)*(float)mc.scale;
    return D2D1::RectF(mcx-dw/2+(float)mc.dx, mcy-dh/2+(float)mc.dy, mcx+dw/2+(float)mc.dx, mcy+dh/2+(float)mc.dy);
}

// ---------------------------------------------------------------- render
static D2D1_COLOR_F HexColor(const std::wstring& h){
    auto hx=[](wchar_t c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 0; };
    if(h.size()>=7&&h[0]==L'#')
        return D2D1::ColorF((hx(h[1])*16+hx(h[2]))/255.0f,(hx(h[3])*16+hx(h[4]))/255.0f,(hx(h[5])*16+hx(h[6]))/255.0f,1.0f);
    return D2D1::ColorF(0.6f,0.8f,0.4f);
}

// ---------------------------------------------------------------- menu panel
// Раскладка меню считается тем же кодом, что и рисование: «сухой» проход
// (dry=true, без DrawText) доходит до конца и возвращает точную высоту панели —
// ничего не срезается и не съезжает ни при каком состоянии.
static float g_menuY=10.0f;
static void DrawMenuBody(ID2D1RenderTarget* rt,ID2D1SolidColorBrush* br,D2D1_COLOR_F accent,bool dry){
    float x0=14.0f, x1=MENU_PANEL_W-14.0f;
    auto tRow=[&](const wchar_t* txt,IDWriteTextFormat* tf,float h)->void{
        if(rt) rt->DrawText(txt,(UINT32)wcslen(txt),tf,D2D1::RectF(x0+8,g_menuY,x1-6,g_menuY+h+2),br);
        g_menuY+=h;
    };
    auto button=[&](const wchar_t* id,const std::wstring& txt,D2D1_COLOR_F fill,D2D1_COLOR_F tcol,float h)->void{
        D2D1_RECT_F r=D2D1::RectF(x0,g_menuY,x1,g_menuY+h);
        if(rt){
            br->SetColor(fill);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),br);
            br->SetColor(tcol);
            rt->DrawText(txt.c_str(),(UINT32)txt.size(),g.tfNorm,D2D1::RectF(r.left+10,r.top,r.right-10,r.bottom),br);
            g.buttons.push_back({id,r});
        }
        g_menuY+=h+7;
    };
    auto section=[&](const wchar_t* txt)->void{
        if(rt){
            br->SetColor(D2D1::ColorF(1,1,1,0.5f));
            rt->DrawText(txt,(UINT32)wcslen(txt),g.tfSmall,D2D1::RectF(x0+4,g_menuY,MENU_PANEL_W,g_menuY+16),br);
            br->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,0.35f));
            rt->DrawLine(D2D1::Point2F(x0+4,g_menuY+18),D2D1::Point2F(x1-4,g_menuY+18),br,1.0f);
        }
        g_menuY+=24;
    };

    // заголовок
    if(rt){
        br->SetColor(D2D1::ColorF(1,1,1,0.97f));
        rt->DrawText(APP_NAME,(UINT32)wcslen(APP_NAME),g.tfTitle,D2D1::RectF(x0+4,g_menuY,MENU_PANEL_W,g_menuY+24),br);
        br->SetColor(D2D1::ColorF(1,1,1,0.45f));
        rt->DrawText(APP_SUBTITLE,(UINT32)wcslen(APP_SUBTITLE),g.tfSmall,D2D1::RectF(x0+4,g_menuY+24,MENU_PANEL_W,g_menuY+38),br);
        br->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,0.8f));
        rt->DrawLine(D2D1::Point2F(x0,g_menuY+40),D2D1::Point2F(x1,g_menuY+40),br,1.5f);
    }
    g_menuY+=46;
    // вселенная (выбор игры)
    button(L"universe",g.uniOpen?L"Вселенная GTA   ▲":L"Вселенная GTA   ▼",
        g.uniOpen?D2D1::ColorF(accent.r,accent.g,accent.b,0.35f):D2D1::ColorF(0.12f,0.14f,0.22f,1.0f),
        D2D1::ColorF(1,1,1,0.95f),30);
    if(g.uniAnim>0.05f){
        float slide=(1.0f-g.uniAnim)*18.0f;
        for(int i=0;i<(int)g.games.size();i++){
            D2D1_RECT_F r=D2D1::RectF(x0+4,g_menuY+i*30-slide,x1-4,g_menuY+i*30+24-slide);
            D2D1_COLOR_F gcol=HexColor(g.games[i].colorHex);
            bool act=(i==g.activeGame);
            if(rt){
                br->SetColor(D2D1::ColorF(act?gcol.r:0.12f,act?gcol.g:0.14f,act?gcol.b:0.22f,g.uniAnim));
                rt->FillRoundedRectangle(D2D1::RoundedRect(r,6,6),br);
                br->SetColor(D2D1::ColorF(act?0:1,act?0:1,act?0:1,0.9f*g.uniAnim));
                rt->DrawText(g.games[i].title.c_str(),(UINT32)g.games[i].title.size(),g.tfNorm,D2D1::RectF(r.left+12,r.top,r.right-8,r.bottom),br);
                g.buttons.push_back({L"game"+std::to_wstring(i),r});
            }
        }
        g_menuY+=(float)g.games.size()*30*g.uniAnim;
    }

    // переключатель издания (не для всех игр: у GTA IV/V одна версия)
    if(CurGame().hasEditions){
        std::wstring el=g.definitiveEdition?L"Definitive Edition (.sav)":L"Обычная версия (.b)";
        D2D1_RECT_F r=D2D1::RectF(x0,g_menuY,x1,g_menuY+30);
        if(rt){
            br->SetColor(g.definitiveEdition?D2D1::ColorF(0.20f,0.32f,0.18f,1.0f):D2D1::ColorF(0.20f,0.22f,0.32f,1.0f));
            rt->FillRoundedRectangle(D2D1::RoundedRect(r,7,7),br);
            br->SetColor(D2D1::ColorF(1,1,1,0.96f));
            rt->DrawText(el.c_str(),(UINT32)el.size(),g.tfNorm,D2D1::RectF(r.left+10,r.top,r.right-10,r.bottom),br);
            g.buttons.push_back({L"edition",r});
        }
        g_menuY+=30+10;
    }

    // коллекции
    section(L"Коллекции");
    int totalLeft=0,totalAll=0,ci=0;
    for(auto&c:g.cats){
        auto&fl=FoundFlags(c.name);
        int fn=0; for(int f:fl) fn+=f;
        int all=(int)c.spots.size();
        bool hidden=g.hiddenCats[c.name]==1;
        // прогресс из сейва: показываем сейв, если он даёт больше/иной прогресс
        auto sc=g.saveCounts.find(c.name);
        bool hasSync=sc!=g.saveCounts.end();
        int shown=hasSync?tmax(fn,sc->second):fn;
        if(shown>all) shown=all; // счётчик сейва не может превышать размер категории
        if(!hidden){ totalLeft+=all-shown; totalAll+=all; }
        D2D1_RECT_F r=D2D1::RectF(x0,g_menuY,x1,g_menuY+22);
        if(rt){
            if(hidden){ br->SetColor(D2D1::ColorF(0.5f,0.14f,0.2f,0.55f)); rt->FillRoundedRectangle(D2D1::RoundedRect(r,4,4),br); }
            D2D1_COLOR_F col=hidden?D2D1::ColorF(1,1,1,0.35f):((all>0&&shown==all)?D2D1::ColorF(0.4f,0.95f,0.5f,0.95f):D2D1::ColorF(1,1,1,0.9f));
            br->SetColor(col);
            wchar_t b[160];
            wchar_t dispName[170];
            if(c.name.find(L"Seagull")!=std::wstring::npos || c.name.find(L"TBOGT")!=std::wstring::npos || c.name.find(L"TLAD")!=std::wstring::npos)
                swprintf(dispName,170,L"DLC: %s",c.name.c_str());
            else swprintf(dispName,170,L"%s",c.name.c_str());
            bool autoSynced=g.saveCounts.count(c.name)>0; // категория синхронизируется с сейвом
            if(hidden) swprintf(b,160,L"%s — скрыто",dispName);
            else if(hasSync&&sc->second>fn) swprintf(b,160,L"%s — %d/%d (сейв)",dispName,sc->second,all);
            else swprintf(b,160,L"%s — %d/%d",dispName,fn,all);
            rt->DrawText(b,(UINT32)wcslen(b),g.tfNorm,D2D1::RectF(r.left+8,r.top,r.right-8,r.bottom+1),br);
            g.buttons.push_back({L"cat"+std::to_wstring(ci),r});
        }
        ci++;
        g_menuY+=26;
    }
    if(rt){
        br->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,1));
        wchar_t b2[160]; swprintf(b2,160,L"Осталось: %d / %d",totalLeft,totalAll);
        rt->DrawText(b2,(UINT32)wcslen(b2),g.tfNorm,D2D1::RectF(x0+4,g_menuY,MENU_PANEL_W,g_menuY+20),br);
    }
    g_menuY+=26;

    // кнопки действий
    button(L"togglefound",g.showFound?L"Скрыть найденные":L"Показать найденные",D2D1::ColorF(0.12f,0.14f,0.22f,1.0f),D2D1::ColorF(1,1,1,0.9f),30);
    {
        std::wstring bt=g.capturingBind?L"Нажмите любую клавишу…":(L"Бинд оверлея:  "+KeyName(g.hotkeyVK));
        button(L"bind",bt,g.capturingBind?D2D1::ColorF(1.0f,0.6f,0.1f,0.95f):D2D1::ColorF(0.12f,0.14f,0.22f,1.0f),D2D1::ColorF(1,1,1,0.9f),30);
    }
    {
        bool flash=(int)(GetTickCount()-g.saveFlashUntil)<0;
        button(L"save",flash?L"Сохранено ✓":L"Сохранить прогресс",flash?D2D1::ColorF(0.4f,0.95f,0.5f,0.95f):D2D1::ColorF(0.12f,0.14f,0.22f,1.0f),flash?D2D1::ColorF(0,0,0,0.95f):D2D1::ColorF(1,1,1,0.9f),30);
    }
    button(L"calibmode",g.calibMode?L"Калибровка: ВКЛ":L"Калибровка меток",g.calibMode?D2D1::ColorF(1.0f,0.6f,0.1f,0.95f):D2D1::ColorF(0.12f,0.14f,0.22f,1.0f),D2D1::ColorF(1,1,1,0.9f),30);

    // панель калибровки
    if(g.calibMode){
        float bw=(x1-x0-3*6)/4.0f;
        auto calBtn=[&](int row,int col,const wchar_t* label,const wchar_t* id,bool hot){
            D2D1_RECT_F r=D2D1::RectF(x0+col*(bw+6),g_menuY+row*30,x0+col*(bw+6)+bw,g_menuY+row*30+25);
            if(rt){
                br->SetColor(hot?D2D1::ColorF(1.0f,0.6f,0.1f,0.9f):D2D1::ColorF(0.12f,0.14f,0.22f,1.0f));
                rt->FillRoundedRectangle(D2D1::RoundedRect(r,5,5),br);
                br->SetColor(D2D1::ColorF(1,1,1,0.92f));
                rt->DrawText(label,(UINT32)wcslen(label),g.tfSmall,r,br);
                g.buttons.push_back({id,r});
            }
        };
        calBtn(0,0,L"Метки",L"caltgt0",g.calTarget==0); calBtn(0,1,L"Карта",L"caltgt1",g.calTarget==1);
        calBtn(1,0,L"◀",L"calL",false); calBtn(1,1,L"▲",L"calU",false); calBtn(1,2,L"▼",L"calD",false); calBtn(1,3,L"▶",L"calR",false);
        calBtn(2,0,L"− масштаб",L"calsm",false); calBtn(2,1,L"+ масштаб",L"calsp",false);
        Cal& cc=g.calTarget?g.mcals[CurGame().id]:g.cals[CurGame().id];
        calBtn(2,2,cc.fx?L"ось X ✓":L"ось X",L"calfx",cc.fx);
        calBtn(2,3,cc.fy?L"ось Y ✓":L"ось Y",L"calfy",cc.fy);
        calBtn(3,0,L"Сброс",L"calreset",false); calBtn(3,1,L"Сохранить",L"calsave",false); calBtn(3,2,L"Готово",L"caldone",false);
        if(rt){
            wchar_t cb3[128];
            swprintf(cb3,128,L"%s: dx %.0f dy %.0f ×%.3f",g.calTarget?L"Карта":L"Метки",cc.dx,cc.dy,cc.scale);
            br->SetColor(D2D1::ColorF(1,1,1,0.55f));
            rt->DrawText(cb3,(UINT32)wcslen(cb3),g.tfSmall,D2D1::RectF(x0,g_menuY+4*30-16,x1,g_menuY+4*30),br);
        }
        g_menuY+=4*30+6;
    }

    // сохранение
    section(L"Сохранение игры");
    if(rt){
        br->SetColor(D2D1::ColorF(1,1,1,0.8f));
        rt->DrawText(g.saveInfo.c_str(),(UINT32)g.saveInfo.size(),g.tfSmall,D2D1::RectF(x0+4,g_menuY,x1-4,g_menuY+34),br);
        wchar_t sb[128];
        int synced=(int)g.saveCounts.size();
        GameDef& gd=CurGame();
        if(totalAll==0){
            // данные категорий не загрузились — обычная причина: нет папки data рядом с exe
            swprintf(sb,128,L"⚠ data\\%s\\%s не найден!",gd.id.c_str(),gd.dataFile.c_str());
            br->SetColor(D2D1::ColorF(1,0.4f,0.3f,0.95f));
            rt->DrawText(sb,(UINT32)wcslen(sb),g.tfSmall,D2D1::RectF(x0+4,g_menuY+34,x1-4,g_menuY+50),br);
        } else {
            if(synced>0) swprintf(sb,128,L"Синхронизация: %d категорий из сейва ✓",synced);
            else swprintf(sb,128,L"Синхронизация: сейв не распознан");
            br->SetColor(synced>0?D2D1::ColorF(0.4f,0.95f,0.5f,0.85f):D2D1::ColorF(1,0.7f,0.3f,0.85f));
            rt->DrawText(sb,(UINT32)wcslen(sb),g.tfSmall,D2D1::RectF(x0+4,g_menuY+34,x1-4,g_menuY+50),br);
        }
    }
    g_menuY+=52;
}

static void Render(){
    if(!g.rt) return;
    RECT rc; GetClientRect(g.hwnd,&rc);
    float W=(float)tmax((double)rc.right,1.0), H=(float)tmax((double)rc.bottom,1.0);
    PatBlt(g.dibDC,0,0,(int)g.dibW,(int)g.dibH,BLACKNESS); // стереть предыдущий кадр в DIB (DC RT рисует source-over)
    g.rt->BeginDraw();
    g.rt->Clear(D2D1::ColorF(0,0,0,0)); // честная альфа: прозрачно вне UI (ULW, без цветоключа)
    ID2D1SolidColorBrush* br=nullptr;
    g.rt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1),&br);
    GameDef& gd=CurGame();
    D2D1_COLOR_F accent=HexColor(gd.colorHex);

    // ---- карта/иконки: карта статична (без пана и зума), всегда прямой рендер
    if(g.mapVisible&&g.mapBmp) DrawMapAndIcons(g.rt,W,H);
    // ---- menu panel: цельная тёмная панель (ПОВЕРХ карты), высота = точная раскладка
    g.buttons.clear();
    g_menuY=10.0f; DrawMenuBody(nullptr,nullptr,accent,true);            // сухой проход: точная высота
    float menuH=g_menuY+10.0f;
    D2D1_RECT_F panel=D2D1::RectF(0,0,MENU_PANEL_W,menuH);
    br->SetColor(D2D1::ColorF(0.05f,0.06f,0.10f,1.0f));
    g.rt->FillRoundedRectangle(D2D1::RoundedRect(panel,12,12),br);
    br->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,0.9f));
    g.rt->DrawRoundedRectangle(D2D1::RoundedRect(panel,12,12),br);
    g_menuY=10.0f; DrawMenuBody(g.rt,br,accent,false);           // настоящий проход

    // ---- popup card
    if(g.popupCat>=0&&g.popupCat<(int)g.cats.size()&&g.popupIdx>=0&&g.popupIdx<(int)g.cats[g.popupCat].spots.size()){
        Category& c=g.cats[g.popupCat];
        Spot& s=c.spots[g.popupIdx];
        auto&fl=FoundFlags(c.name);
        bool isFound=g.popupIdx<(int)fl.size()&&fl[g.popupIdx]==1;
        D2D1_RECT_F mr=ViewMapRect(W,H);
        D2D1_POINT_2F sp=SpotPos(mr,c.name,g.popupIdx,s.x,s.y);
        float cw=290,ch=160;
        float px=min(max(sp.x+12,MENU_PANEL_W+10),W-cw-10);
        float py=max(10.0f,min(sp.y-40,H-ch-10));
        D2D1_RECT_F card=D2D1::RectF(px,py,px+cw,py+ch);
        br->SetColor(D2D1::ColorF(0.06f,0.07f,0.12f,0.96f));
        g.rt->FillRoundedRectangle(D2D1::RoundedRect(card,10,10),br);
        br->SetColor(D2D1::ColorF(accent.r,accent.g,accent.b,0.9f));
        g.rt->DrawRoundedRectangle(D2D1::RoundedRect(card,10,10),br);
        wchar_t head[200]; swprintf(head,200,L"%s #%d",c.name.c_str(),s.idx);
        br->SetColor(D2D1::ColorF(1,1,1,0.95f));
        g.rt->DrawText(head,(UINT32)wcslen(head),g.tfC,D2D1::RectF(card.left+12,card.top+8,card.right-12,card.top+30),br);
        wchar_t co[96]; swprintf(co,96,L"Координаты: %.2f, %.2f",s.x,s.y);
        br->SetColor(D2D1::ColorF(1,1,1,0.55f));
        g.rt->DrawText(co,(UINT32)wcslen(co),g.tfSmall,D2D1::RectF(card.left+12,card.top+30,card.right-12,card.top+48),br);
        const wchar_t* info=CatInfo(c.name);
        g.rt->DrawText(info,(UINT32)wcslen(info),g.tfSmall,D2D1::RectF(card.left+12,card.top+50,card.right-12,card.top+112),br);
        float pbw=(cw-36.0f)/2.0f;
        D2D1_RECT_F bbtn=D2D1::RectF(card.left+12,card.bottom-42,card.left+12+pbw,card.bottom-12);
        br->SetColor(isFound?D2D1::ColorF(0.4f,0.95f,0.5f,0.9f):D2D1::ColorF(accent.r,accent.g,accent.b,0.9f));
        g.rt->FillRoundedRectangle(D2D1::RoundedRect(bbtn,6,6),br);
        const wchar_t* bt=isFound?L"Нашёл ✓":L"Нашёл";
        br->SetColor(D2D1::ColorF(0,0,0,0.95f));
        g.rt->DrawText(bt,(UINT32)wcslen(bt),g.tfC,bbtn,br);
        g.buttons.push_back({L"mark",bbtn});
        D2D1_RECT_F rvbtn=D2D1::RectF(card.right-12-pbw,card.bottom-42,card.right-12,card.bottom-12);
        br->SetColor(D2D1::ColorF(0.12f,0.14f,0.22f,1.0f));
        g.rt->FillRoundedRectangle(D2D1::RoundedRect(rvbtn,6,6),br);
        br->SetColor(D2D1::ColorF(1,1,1,0.55f));
        g.rt->DrawRoundedRectangle(D2D1::RoundedRect(rvbtn,6,6),br);
        br->SetColor(D2D1::ColorF(1,1,1,0.9f));
        g.rt->DrawText(L"Вернуть",(UINT32)wcslen(L"Вернуть"),g.tfC,rvbtn,br);
        g.buttons.push_back({L"resetspot",rvbtn});
        g.popupRect=card;
    }
    br->Release();
    HRESULT hrEnd=g.rt->EndDraw();
    if(FAILED(hrEnd)){ char b[64]; sprintf(b,"EndDraw fail 0x%08lX",(unsigned long)hrEnd); GTCLOG(b); }
    // вывод кадра: UpdateLayeredWindow прямо из DIB (быстрый путь старого проекта)
    if(g.dibDC&&g.hwnd){
        HDC sdc=GetDC(nullptr);
        POINT src{0,0}; SIZE sz{(LONG)g.dibW,(LONG)g.dibH};
        BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        RECT wr; GetWindowRect(g.hwnd,&wr); POINT pos{wr.left,wr.top};
        UpdateLayeredWindow(g.hwnd,sdc,&pos,&sz,g.dibDC,&src,0,&bf,ULW_ALPHA);
        ReleaseDC(nullptr,sdc);
    }
}
// ---------------------------------------------------------------- hit test
static bool OverUI(float x,float y){
    for(auto&b:g.buttons) if(InRect(b.r,x,y)) return true;
    RECT rc; GetClientRect(g.hwnd,&rc);
    if(g.mapVisible&&g.mapBmp&&InRect(MapDrawRect(ViewMapRect((float)rc.right,(float)rc.bottom)),x,y)) return true;
    if(g.popupCat>=0&&InRect(g.popupRect,x,y)) return true;
    return false;
}
static int HitSpot(float x,float y){
    if(!g.mapVisible||!g.mapBmp) return -1;
    RECT rc; GetClientRect(g.hwnd,&rc);
    D2D1_RECT_F mr=ViewMapRect((float)rc.right,(float)rc.bottom);
    if(!InRect(mr,x,y)) return -1;
    int best=-1; float bestD=15*15;
    for(size_t ci=0;ci<g.cats.size();ci++){
        auto&c=g.cats[ci];
        if(g.hiddenCats[c.name]==1) continue;
        for(size_t si=0;si<c.spots.size();si++){
            D2D1_POINT_2F p=SpotPos(mr,c.name,(int)si,c.spots[si].x,c.spots[si].y);
            float dx=p.x-x,dy=p.y-y,d=dx*dx+dy*dy;
            if(d<bestD){ bestD=d; best=(int)(ci*100000+si); }
        }
    }
    return best;
}

// ---------------------------------------------------------------- actions
static void SyncFromSave();
static void SwitchGame(int i){
    g.activeGame=i; g.popupCat=g.popupIdx=-1;
    g.iconCache.clear();
    ParseDataFile();
    FindNewestSave();
    SyncFromSave();
    LoadMapBitmap();
    SaveState();
    if(i>=0&&i<(int)g.games.size()){
        char idb[32]={0};
        WideCharToMultiByte(CP_UTF8,0,g.games[i].id.c_str(),-1,idb,31,nullptr,nullptr);
        GTCLOGF("game switched -> %s (map %ux%u)",idb,(unsigned)g.mapW,(unsigned)g.mapH);
    }
}
static void ClickBtn(const std::wstring& id){
    if(id.rfind(L"game",0)==0){ SwitchGame(_wtoi(id.c_str()+4)); return; }
    if(id==L"edition"){ g.definitiveEdition=!g.definitiveEdition; FindNewestSave(); SyncFromSave(); SaveState(); return; }
    if(id==L"togglefound"){ g.showFound=!g.showFound; return; }
    if(id==L"bind"){
        if(!g.capturingBind){
            g.capturingBind=true;
            g.capKbd=SetWindowsHookExW(WH_KEYBOARD_LL,CaptureKbdProc,nullptr,0);
            g.capMouse=SetWindowsHookExW(WH_MOUSE_LL,CaptureMouseProc,nullptr,0);
        } else EndBindCapture();
        return;
    }
    if(id==L"universe"){ g.uniOpen=!g.uniOpen; SetTimer(g.hwnd,TIMER_UNIVERSE,16,nullptr); return; }
    if(id.rfind(L"cat",0)==0){ int ci=_wtoi(id.c_str()+3); if(ci>=0&&ci<(int)g.cats.size()){ auto&hc=g.hiddenCats[g.cats[ci].name]; hc=hc?0:1; SaveState(); } return; }
    if(id==L"save"){ SaveState(); g.saveFlashUntil=GetTickCount()+1500; return; }
    if(id==L"calibmode"){ g.calibMode=!g.calibMode; return; }
    if(id==L"caldone"){ g.calibMode=false; SaveState(); return; }
    if(id==L"calsave"){ SaveState(); g.saveFlashUntil=GetTickCount()+1500; return; }
    if(id.rfind(L"cal",0)==0){
        if(id==L"caltgt0"){ g.calTarget=0; return; }
        if(id==L"caltgt1"){ g.calTarget=1; return; }
        Cal& cl=g.calTarget?g.mcals[CurGame().id]:g.cals[CurGame().id];
        double step=10.0;
        if(id==L"calL") cl.dx-=step;
        else if(id==L"calR") cl.dx+=step;
        else if(id==L"calU") cl.dy-=step;
        else if(id==L"calD") cl.dy+=step;
        else if(id==L"calsp") cl.scale*=1.03;
        else if(id==L"calsm") cl.scale/=1.03;
        else if(id==L"calfx") cl.fx=!cl.fx;
        else if(id==L"calfy") cl.fy=!cl.fy;
        else if(id==L"calreset") cl=Cal();
        return;
    }
    if(id==L"resetspot"&&g.popupCat>=0&&g.popupIdx>=0&&g.popupCat<(int)g.cats.size()){
        // «Вернуть»: показать обратно иконку, скрытую отметкой «Нашёл»
        auto&fl=FoundFlags(g.cats[g.popupCat].name);
        if((int)fl.size()>g.popupIdx) fl[g.popupIdx]=0;
        GTCLOGF("spot returned (%d.%d)",g.popupCat,g.popupIdx);
        SaveState();
        g.popupCat=g.popupIdx=-1;
        return;
    }
    if(id==L"mark"&&g.popupCat>=0&&g.popupIdx>=0){
        auto&fl=FoundFlags(g.cats[g.popupCat].name);
        if((int)fl.size()<=g.popupIdx) fl.resize(g.popupIdx+1,0);
        fl[g.popupIdx]=1;
        GTCLOGF("spot found (%d.%d)",g.popupCat,g.popupIdx);
        SaveState();
        g.popupCat=g.popupIdx=-1;
    }
}

// ---------------------------------------------------------------- ULW resize/present
static HWND FindGameWindow(HWND self){
    struct Ctx { HWND self, found; DWORD maxArea; };
    Ctx ctx{ self, nullptr, 0 };
    EnumWindows([](HWND hw, LPARAM lp)->BOOL {
        Ctx* c=(Ctx*)lp;
        if(hw==c->self||!IsWindowVisible(hw)) return TRUE;
        LONG exs=(LONG)GetWindowLongPtrW(hw,GWL_EXSTYLE);
        if(exs&WS_EX_TOOLWINDOW) return TRUE; // тултипы/палитры — не окна игры
        wchar_t cls[64]={0}, title[128]={0};
        GetClassNameW(hw,cls,64); GetWindowTextW(hw,title,128);
        if(wcsstr(title,L"Grand Theft Auto")==nullptr) return TRUE; // только настоящие окна игры
        if(!wcscmp(cls,L"CabinetWClass")||!wcscmp(cls,L"ExploreWClass")) return TRUE; // проводник с такой папкой
        RECT r, cr; if(!GetWindowRect(hw,&r)||!GetClientRect(hw,&cr)) return TRUE;
        // окно должно быть здоровенного: иначе это лаунчер/ярлык/свернутое — оверлей на нём развалится
        if(cr.right-cr.left<800||cr.bottom-cr.top<600) return TRUE;
        DWORD area=(DWORD)((r.right-r.left)*(r.bottom-r.top));
        if(area>c->maxArea){ c->maxArea=area; c->found=hw; }
        return TRUE;
    },(LPARAM)&ctx);
    return ctx.found;
}

static void ResizeTargets(){
    if(!g.hwnd) return;
    RECT rc; GetClientRect(g.hwnd,&rc);
    UINT w=(UINT)tmax((LONG)rc.right,1L), hgt=(UINT)tmax((LONG)rc.bottom,1L);
    if(g.rt&&w==g.rtW&&hgt==g.rtH){ return; }
    SafeRelease(&g.rt); SafeRelease(&g.wicBmp);
    SafeRelease(&g.mapBmp); SafeRelease(&g.mapBmp2); SafeRelease(&g.mapBmp4); g.mapBmpFor=L"";
    for(auto&kv:g.iconCache) SafeRelease(&kv.second);
    g.iconCache.clear();
    g.rtW=w; g.rtH=hgt;
    // DIB-буфер + DC RenderTarget: D2D рисует прямо в DIB, кадр уходит на экран через
    // UpdateLayeredWindow (как в старом SA_Map_Overlay — без StretchDIBits и цветоключа)
    if(g.dibDC){ DeleteDC(g.dibDC); g.dibDC=nullptr; }
    if(g.dibHbm){ DeleteObject(g.dibHbm); g.dibHbm=nullptr; }
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=(LONG)w; bi.bmiHeader.biHeight=-(LONG)hgt; // top-down
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    g.dibDC=CreateCompatibleDC(nullptr);
    g.dibHbm=CreateDIBSection(g.dibDC,&bi,DIB_RGB_COLORS,nullptr,0,0);
    if(!g.dibHbm){ GTCLOG("dib fail"); return; }
    SelectObject(g.dibDC,g.dibHbm);
    g.dibW=w; g.dibH=hgt;
    D2D1_RENDER_TARGET_PROPERTIES rtp=D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),
        0,0,D2D1_RENDER_TARGET_USAGE_NONE);
    ID2D1DCRenderTarget* dcrt=nullptr;
    if(FAILED(g.d2dFactory->CreateDCRenderTarget(&rtp,&dcrt))){ GTCLOG("dcrt fail"); return; }
    g.rt=dcrt;
    RECT rr{0,0,(LONG)w,(LONG)hgt};
    if(FAILED(dcrt->BindDC(g.dibDC,&rr))){ GTCLOG("binddc fail"); return; }
    LoadMapBitmap();
    GTCLOG("resize ok");
}

// ---------------------------------------------------------------- click-through
// Вне меню/карты/карточки оверлей полностью прозрачен для мыши (WS_EX_TRANSPARENT),
// чтобы клики уходили в игру; над UI — обычное поведение.
static void UpdateClickThrough(HWND h){
    // Оверлей должен получать мышь: убираем click-through.
    LONG ex=(LONG)GetWindowLongPtrW(h,GWL_EXSTYLE);
    LONG want=ex & ~WS_EX_TRANSPARENT;
    if(want!=ex) SetWindowLongPtrW(h,GWL_EXSTYLE,want);
}

// ---------------------------------------------------------------- bind hooks
// Окно оверлея никогда не в фокусе, поэтому клавишу ловим низкоуровневыми хуками.
static void ToggleOverlay(){
    if(IsWindowVisible(g.hwnd)){
        ShowWindow(g.hwnd,SW_HIDE);
        // Вернуть фокус игре после закрытия оверлея.
        if(g.previousForeground && IsWindow(g.previousForeground))
            SetForegroundWindow(g.previousForeground);
        g.previousForeground=nullptr;
    } else {
        // Запомнить игру/окно, которое было активно до открытия.
        g.previousForeground=GetForegroundWindow();

        // Разрешить окну оверлея получать фокус и ввод мыши.
        LONG ex=(LONG)GetWindowLongPtrW(g.hwnd,GWL_EXSTYLE);
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        SetWindowLongPtrW(g.hwnd,GWL_EXSTYLE,ex);
        SetWindowPos(g.hwnd,HWND_TOPMOST,0,0,0,0,
            SWP_NOMOVE|SWP_NOSIZE|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);

        ShowWindow(g.hwnd,SW_SHOW);
        SetForegroundWindow(g.hwnd);
        SetActiveWindow(g.hwnd);
        SetFocus(g.hwnd);
        UpdateClickThrough(g.hwnd);
        Render();
    }
}
static void EndBindCapture(){
    g.capturingBind=false;
    if(g.capKbd){ UnhookWindowsHookEx(g.capKbd); g.capKbd=nullptr; }
    if(g.capMouse){ UnhookWindowsHookEx(g.capMouse); g.capMouse=nullptr; }
}
static LRESULT CALLBACK CaptureKbdProc(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&g.capturingBind&&wp==WM_KEYDOWN){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)lp;
        UINT vk=k->vkCode;
        if(vk==VK_ESCAPE){ EndBindCapture(); PostMessageW(g.hwnd,WM_BIND_END,0,0); return 1; }
        EndBindCapture();
        PostMessageW(g.hwnd,WM_BIND_VK,vk,0);
        return 1; // съедаем нажатие
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static LRESULT CALLBACK CaptureMouseProc(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&g.capturingBind){
        UINT vk=0;
        // ЛКМ и ПКМ нельзя ставить на бинд — клик проходит дальше как обычный
        if(wp==WM_MBUTTONDOWN) vk=VK_MBUTTON;
        else if(wp==WM_XBUTTONDOWN){ MSLLHOOKSTRUCT* m=(MSLLHOOKSTRUCT*)lp; vk=(HIWORD(m->mouseData)==XBUTTON1)?VK_XBUTTON1:VK_XBUTTON2; }
        if(vk){ EndBindCapture(); PostMessageW(g.hwnd,WM_BIND_VK,vk,0); return 1; }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static LRESULT CALLBACK PersistKbdProc(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&wp==WM_KEYDOWN&&!g.capturingBind){
        KBDLLHOOKSTRUCT* k=(KBDLLHOOKSTRUCT*)lp;
        if(k->vkCode==g.hotkeyVK){ PostMessageW(g.hwnd,WM_TOGGLE,0,0); return 1; }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static LRESULT CALLBACK PersistMouseProc(int code,WPARAM wp,LPARAM lp){
    if(code>=0&&!g.capturingBind){
        UINT vk=0;
        if(wp==WM_LBUTTONDOWN) vk=VK_LBUTTON;
        else if(wp==WM_RBUTTONDOWN) vk=VK_RBUTTON;
        else if(wp==WM_MBUTTONDOWN) vk=VK_MBUTTON;
        else if(wp==WM_XBUTTONDOWN){ MSLLHOOKSTRUCT* m=(MSLLHOOKSTRUCT*)lp; vk=(HIWORD(m->mouseData)==XBUTTON1)?VK_XBUTTON1:VK_XBUTTON2; }
        if(vk&&vk==g.hotkeyVK){ PostMessageW(g.hwnd,WM_TOGGLE,0,0); return 1; }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
static void InstallPersistHook(){
    if(g.hookKbd){ UnhookWindowsHookEx(g.hookKbd); g.hookKbd=nullptr; }
    if(g.hookMouse){ UnhookWindowsHookEx(g.hookMouse); g.hookMouse=nullptr; }
    if(g.hotkeyVK>=VK_LBUTTON&&g.hotkeyVK<=VK_XBUTTON2){
        g.mouseBind=true;
        g.hookMouse=SetWindowsHookExW(WH_MOUSE_LL,PersistMouseProc,nullptr,0);
    } else {
        g.mouseBind=false;
        if(!RegisterHotKey(g.hwnd,HOTKEY_MAP,0,g.hotkeyVK))
            g.hookKbd=SetWindowsHookExW(WH_KEYBOARD_LL,PersistKbdProc,nullptr,0);
    }
}

// ---------------------------------------------------------------- wndproc
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_CREATE: SetTimer(h,TIMER_UNCLIP,120,nullptr); SetTimer(h,TIMER_TOPMOST,2000,nullptr); SetTimer(h,TIMER_SAVECHECK,3000,nullptr); SetTimer(h,6,16,nullptr); return 0;
    case WM_TIMER:
        if(wp==TIMER_UNCLIP&&IsWindowVisible(h)){ UpdateClickThrough(h); }
        if(wp==6&&mapZooming&&IsWindowVisible(h)){ ProcessMapZoom(); return 0; }
        if(wp==TIMER_SAVECHECK&&IsWindowVisible(h)){
            std::wstring oldPath=g.savePath; FILETIME oldM=g.saveMtime;
            FindNewestSave();
            if(oldPath!=g.savePath||CompareFileTime(&oldM,&g.saveMtime)!=0){
                SyncFromSave(); // новый сейв или перезапись существующего
                Render();
            }
        }
        if(wp==TIMER_UNIVERSE){
            float target=g.uniOpen?1.0f:0.0f;
            g.uniAnim+=(target-g.uniAnim)*0.25f;
            if(g.uniAnim<0.002f)g.uniAnim=0; if(g.uniAnim>0.998f)g.uniAnim=1;
            Render();
            if(g.uniAnim==target){ KillTimer(h,TIMER_UNIVERSE); if(!g.uniOpen) g.uniAnim=0; }
            return 0;
        }
        if(wp==TIMER_TOPMOST&&IsWindowVisible(h)){
            SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            // привязка к окну игры ТОЛЬКО когда игра активна или на весь экран:
            // иначе оверлей не должен прыгать за окном, которое висит на фоне
            HWND gh=FindGameWindow(h);
            bool follow=false;
            RECT gr{};
            if(gh){
                GetClientRect(gh,&gr);
                HWND fg=GetForegroundWindow();
                RECT wr{};
                if(GetWindowRect(gh,&wr)){
                    bool fullscreen=(wr.right-wr.left>=GetSystemMetrics(SM_CXSCREEN)-8 &&
                                     wr.bottom-wr.top>=GetSystemMetrics(SM_CYSCREEN)-8);
                    if(fullscreen||fg==gh) follow=true;
                }
            }
            int scrW=GetSystemMetrics(SM_CXSCREEN), scrH=GetSystemMetrics(SM_CYSCREEN);
            if(follow&&gr.right>0&&gr.bottom>0){
                POINT pt{0,0}; ClientToScreen(gh,&pt);
                SetWindowPos(h,nullptr,pt.x,pt.y,gr.right,gr.bottom,SWP_NOACTIVATE|SWP_NOZORDER);
                if(gr.right!=g.lastFollowW||gr.bottom!=g.lastFollowH){ // логируем ТОЛЬКО смену цели
                    static wchar_t tbuf[128]={0};
                    wchar_t cbuf[64]={0}; GetWindowTextW(gh,cbuf,64); GetWindowTextW(gh,tbuf,128);
                    char idb[64]={0};
                    WideCharToMultiByte(CP_UTF8,0,tbuf,-1,idb,63,nullptr,nullptr);
                    GTCLOGF("follow window %ls %ux%u \"%s\"",(wchar_t*)cbuf,(unsigned)gr.right,(unsigned)gr.bottom,idb);
                    g.lastFollowW=gr.right; g.lastFollowH=gr.bottom;
                }
            } else if(g.lastFollowW!=scrW||g.lastFollowH!=scrH){
                // игра не активна — вернуть оверлей на весь экран
                SetWindowPos(h,nullptr,0,0,scrW,scrH,SWP_NOACTIVATE|SWP_NOZORDER);
                if(g.lastFollowW!=scrW||g.lastFollowH!=scrH)
                    GTCLOGF("follow off -> fullscreen %ux%u",(unsigned)scrW,(unsigned)scrH);
                g.lastFollowW=scrW; g.lastFollowH=scrH;
            }
        }
        return 0;
    case WM_KEYDOWN: return 0; // клавиатура ловится LL-хуками (окно не в фокусе)
    case WM_BIND_VK: { // новый бинд из хука
        if(wp==VK_LBUTTON||wp==VK_RBUTTON) return 0; // запрещено
        g.hotkeyVK=(UINT)wp;
        UnregisterHotKey(h,HOTKEY_MAP);
        InstallPersistHook();
        SaveState();
        Render();
        return 0;
    }
    case WM_TOGGLE: ToggleOverlay(); return 0;
    case WM_BIND_END: Render(); return 0;
    case WM_HOTKEY:
        if(wp==HOTKEY_MAP) ToggleOverlay();
        return 0;
    case WM_PAINT: ValidateRect(h,nullptr); return 0;
    case WM_SIZE: ResizeTargets(); Render(); return 0;
    case WM_NCHITTEST:
        // The overlay is interactive while open; receive mouse input.
        return HTCLIENT;
    case WM_MOUSEMOVE: {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(mapDragging){
            viewTx=mapDragTx+(pt.x-mapDragX); viewTy=mapDragTy+(pt.y-mapDragY);
            Render(); return 0;
        } else if(g.spotDrag&&g.mapVisible&&g.dragSpotCat>=0){
            // перенос ОДНОЙ метки (смещение в px карты)
            D2D1_POINT_2F off{ g.spotDrag0.x+(pt.x-g.dragStart.x)/(float)g.spotDragScale,
                               g.spotDrag0.y+(pt.y-g.dragStart.y)/(float)g.spotDragScale };
            g.spotOffs[SpotKey(g.cats[g.dragSpotCat].name,g.dragSpotIdx)]=off;
            Render();
        } else if(g.calibDrag&&g.mapVisible){ // перетаскивание меток/карты — только в режиме калибровки
            Cal& cl=g.calTarget?g.mcals[CurGame().id]:g.cals[CurGame().id];
            cl.dx=g.calDx0+(pt.x-g.dragStart.x);
            cl.dy=g.calDy0+(pt.y-g.dragStart.y);
            Render();
        } // сама карта статична: перетаскивать её нельзя
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        for(auto&b:g.buttons) if(InRect(b.r,(float)pt.x,(float)pt.y)){ ClickBtn(b.id); Render(); return 0; }
        if(g.popupCat>=0&&!InRect(g.popupRect,(float)pt.x,(float)pt.y)){ g.popupCat=g.popupIdx=-1; Render(); return 0; }
        if(g.mapVisible&&g.mapBmp){
            if(g.calibMode){ // перетаскивание: одна метка / слой меток / карта
                RECT rc2; GetClientRect(h,&rc2);
                D2D1_RECT_F mrc=MapDrawRect(ViewMapRect((float)rc2.right,(float)rc2.bottom));
                if(!InRect(mrc,(float)pt.x,(float)pt.y)) return 0;
                if(g.calTarget==0){
                    int hs=HitSpot((float)pt.x,(float)pt.y);
                    if(hs>=0){ // захват ОДНОЙ метки
                        g.spotDrag=true; g.dragStart=pt;
                        g.dragSpotCat=hs/100000; g.dragSpotIdx=hs%100000;
                        D2D1_POINT_2F off{0,0};
                        auto it=g.spotOffs.find(SpotKey(g.cats[g.dragSpotCat].name,g.dragSpotIdx));
                        if(it!=g.spotOffs.end()) off=it->second;
                        g.spotDrag0=off;
                        D2D1_RECT_F mrr=ViewMapRect((float)rc2.right,(float)rc2.bottom);
                        g.spotDragScale=(mrr.right-mrr.left)/(float)g.mapW; // экран -> px карты
                        SetCapture(h);
                        return 0;
                    }
                    if(g.calTarget==0){ // слой целиком
                        g.calibDrag=true; g.dragStart=pt;
                        Cal& cl=g.cals[CurGame().id];
                        g.calDx0=cl.dx; g.calDy0=cl.dy;
                        SetCapture(h);
                    }
                    return 0;
                }
                g.calibDrag=true; g.dragStart=pt;
                Cal& cl=g.mcals[CurGame().id];
                g.calDx0=cl.dx; g.calDy0=cl.dy;
                SetCapture(h);
                return 0;
            }
            int hs=HitSpot((float)pt.x,(float)pt.y);
            if(hs>=0){ g.popupCat=hs/100000; g.popupIdx=hs%100000; Render(); return 0; }
            // Перетаскивание карты в обычном режиме.
            RECT rc2; GetClientRect(h,&rc2);
            D2D1_RECT_F viewRect=MapDrawRect(ViewMapRect((float)rc2.right,(float)rc2.bottom));
            if(InRect(viewRect,(float)pt.x,(float)pt.y)){
                mapDragging=true; mapDragX=pt.x; mapDragY=pt.y;
                mapDragTx=viewTx; mapDragTy=viewTy; mapZooming=false;
                SetCapture(h); return 0;
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: if(mapDragging){ mapDragging=false; ReleaseCapture(); return 0; } if(g.calibDrag||g.spotDrag){
        POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        bool moved=abs(pt.x-g.dragStart.x)>2||abs(pt.y-g.dragStart.y)>2;
        g.calibDrag=false; g.spotDrag=false; g.dragSpotCat=-1;
        ReleaseCapture();
        GTCLOG("drag end (calibration/label)");
        if(moved) SaveState();
    } return 0;
    case WM_MOUSEWHEEL: {
        POINT pt; pt.x=GET_X_LPARAM(lp); pt.y=GET_Y_LPARAM(lp); ScreenToClient(h,&pt);
        RECT rc; GetClientRect(h,&rc);
        D2D1_RECT_F mr=MapDrawRect(ViewMapRect((float)rc.right,(float)rc.bottom));
        if(!InRect(mr,(float)pt.x,(float)pt.y)) return 0;
        if(g.calibMode){ // в режиме калибровки колесо = масштаб меток/карты
            Cal& cl=g.calTarget?g.mcals[CurGame().id]:g.cals[CurGame().id];
            cl.scale*=((short)HIWORD(wp)>0)?1.05:1/1.05;
            if(cl.scale<0.2)cl.scale=0.2; if(cl.scale>5.0)cl.scale=5.0;
            GTCLOGF("calib scale %.3f (%ls)",cl.scale,g.calTarget?L"map":L"labels");
            Render();
            return 0;
        }
        // Плавный зум с привязкой к точке под курсором.
        float k=((short)HIWORD(wp)>0)?1.25f:1.0f/1.25f;
        float tgt=viewScale*k;
        if(tgt>10.0f)tgt=10.0f; if(tgt<0.08f)tgt=0.08f;
        mapZoomAnchorX=(float)pt.x; mapZoomAnchorY=(float)pt.y;
        mapZoomBaseTx=viewTx; mapZoomBaseTy=viewTy; mapZoomBaseScale=viewScale;
        mapZoomTarget=tgt; mapZooming=true; mapDragging=false;
        SetTimer(h,6,16,nullptr);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        g.popupCat=g.popupIdx=-1;
        if(g.calibMode&&g.calTarget==0){ // ПКМ по метке в калибровке = сброс её сдвига
            POINT pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            int hs=HitSpot((float)pt.x,(float)pt.y);
            if(hs>=0){
                g.spotOffs.erase(SpotKey(g.cats[hs/100000].name,hs%100000));
                SaveState();
            }
        }
        Render(); return 0;
    }
    case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0;
    case WM_DESTROY: KillTimer(h,TIMER_UNCLIP); KillTimer(h,TIMER_TOPMOST); KillTimer(h,TIMER_SAVECHECK); KillTimer(h,TIMER_UNIVERSE); KillTimer(h,6); PostQuitMessage(0); return 0;
    case WM_TRAYICON:
        if(lp==WM_LBUTTONUP||lp==WM_RBUTTONUP){
            POINT pt; GetCursorPos(&pt);
            HMENU menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING,1,L"Открыть");
            AppendMenuW(menu,MF_STRING,2,L"Выход");
            SetForegroundWindow(h);
            int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,h,nullptr);
            DestroyMenu(menu);
            if(cmd==1) ShowWindow(h,SW_SHOWNOACTIVATE);
            if(cmd==2){ Shell_NotifyIconW(NIM_DELETE,&g.nid); DestroyWindow(h); }
        }
        return 0;
    }
    return DefWindowProcW(h,m,wp,lp);
}

// ---------------------------------------------------------------- init
static void InitD2D(){
    D2D1_FACTORY_OPTIONS fo{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,__uuidof(ID2D1Factory1),&fo,(void**)&g.d2dFactory);
    CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),(void**)&g.wic);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),(IUnknown**)&g.dwrite);
    g.dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"ru-ru",&g.tfTitle);
    g.dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"ru-ru",&g.tfNorm);
    g.dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12,L"ru-ru",&g.tfSmall);
    g.dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"ru-ru",&g.tfC);
    if(g.tfC) g.tfC->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); // центрированный (карточка иконки)
}

static void GTCLOG(const char* msg){
    static int failures=0;
    if(failures>32) return;
    // ротация: при переполнении (~2 МБ) архивируем в log.old.txt и начинаем заново
    static DWORD lastRotCheck=0;
    DWORD now=GetTickCount();
    if(now-lastRotCheck>15000){
        lastRotCheck=now;
        const std::wstring& dir=AppDataDir()+L"\\GrandTheftCompletionist";
        HANDLE hf=CreateFileW((dir+L"\\log.txt").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(hf!=INVALID_HANDLE_VALUE){
            LARGE_INTEGER sz; BOOL ok=GetFileSizeEx(hf,&sz);
            CloseHandle(hf);
            if(ok&&sz.QuadPart>2*1024*1024){
                MoveFileExW((dir+L"\\log.txt").c_str(),(dir+L"\\log.old.txt").c_str(),MOVEFILE_REPLACE_EXISTING);
            }
        }
    }
    static DWORD tick0=GetTickCount();
    FILE* f=_wfopen((AppDataDir()+L"\\GrandTheftCompletionist\\log.txt").c_str(),L"a");
    if(f){ fprintf(f,"[%9.2fs] %s\n",(GetTickCount()-tick0)/1000.0,msg); fclose(f); }
    if(strstr(msg,"fail")) failures++;
}
static void GTCLOGF(const char* fmt,...){
    char buf[256];
    va_list ap; va_start(ap,fmt);
    vsnprintf(buf,sizeof buf,fmt,ap);
    buf[sizeof buf-1]=0;
    va_end(ap);
    GTCLOG(buf);
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){
    GTCLOG("start");
    CoInitialize(nullptr);
    SetProcessDPIAware();
    LoadGames();
    GTCLOG("games");
    InitD2D();
    GTCLOG("d2d");
    LoadState();
    ParseDataFile();
    FindNewestSave();
    GTCLOG("data");

    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"GTC_Overlay_Mutex");
    bool already=(GetLastError()==ERROR_ALREADY_EXISTS);

    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.hInstance=hi;
    wc.lpfnWndProc=WndProc; wc.lpszClassName=APP_WND;
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExW(&wc);

    g.hIcon=(HICON)LoadImageW(nullptr,(ExeDir()+L"\\assets\\app.ico").c_str(),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_LOADFROMFILE);

    if(already){
        // bring existing instance up
        HWND ex=FindWindowW(APP_WND,nullptr);
        if(ex){ ShowWindow(ex,SW_SHOWNOACTIVATE); }
        return 0;
    }

    HWND h=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,APP_WND,APP_NAME,
        WS_POPUP,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),nullptr,nullptr,hi,nullptr);
    g.hwnd=h;
    GTCLOG("window");

    ResizeTargets();
    SyncFromSave(); // первый прогон синхронизации (нужен валидный rt)
    GTCLOG("initdone");

    // tray icon
    g.nid.cbSize=sizeof(g.nid); g.nid.hWnd=h; g.nid.uID=1;
    g.nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    g.nid.uCallbackMessage=WM_TRAYICON; g.nid.hIcon=g.hIcon;
    wcscpy(g.nid.szTip,APP_NAME);
    Shell_NotifyIconW(NIM_ADD,&g.nid);

    RegisterHotKey(h,HOTKEY_MAP,0,g.hotkeyVK); // бинд настраивается в меню
    InstallPersistHook(); // мышь/фолбэк, если сохранённый бинд не через RegisterHotKey

    ShowWindow(h,SW_SHOWNOACTIVATE);
    Render(); // первый полный кадр: WM_SIZE после ShowWindow не приходит (размер не менялся)
    InvalidateRect(h,nullptr,TRUE);

    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    SaveState();
    return 0;
}
// ---- per-item через скриптовые глобальные переменные (SCR\0) ----
// Для III classic: USJ=SCR+3144 (20dword), Rampage=SCR+3460 (20dword)
// Для VC classic: USJ=SCR+3192 (36dword), Rampage=SCR+5768 (35dword), Robberies=SCR+3556(3)+SCR+6188(12)
// Для VC DE: base = SCR_tag + 1272 вместо +16
static void SyncIVCPerItem(const std::vector<uint8_t>& b,bool vc,bool isDE,const std::vector<long>& statsVals){
    // найти SCR\0
    size_t scrTag=(size_t)-1;
    for(size_t i=0;i+4<=b.size();i++){
        if(b[i]=='S'&&b[i+1]=='C'&&b[i+2]=='R'&&b[i+3]==0){ scrTag=i; break; }
    }
    if(scrTag==(size_t)-1){ GTCLOG("peritem: SCR not found"); return; }
    // база глобальных переменных
    size_t gbase = isDE ? (scrTag+1272) : (scrTag+16);
    // helper: читать dword глобальную переменную $N (1-based)
    auto gvar=[&](int N)->uint32_t{
        size_t off=gbase+(size_t)(N-1)*4;
        if(off+4>b.size()) return 0xFFFFFFFF;
        return Rd32(b,off);
    };
    // Stunt Jumps per-item
    if(Category* c=FindCatSub(L"Stunt")){
        int nJ=vc?36:20;
        int varStart=vc?795:783; // VC: $795-$830, III: $783-$802
        size_t usjOff=gbase+(size_t)(varStart-1)*4;
        std::vector<int> fl((int)c->spots.size(),0);
        int cnt=0; bool valid=true;
        for(int i=0;i<nJ&&valid;i++){
            size_t off=usjOff+(size_t)i*4;
            if(off+4>b.size()){ valid=false; break; }
            uint32_t v=Rd32(b,off);
            if(v==0||v==1){ fl[i]=(int)v; cnt+=(int)v; }
            else valid=false;
        }
        if(valid&&cnt>0){
            // валидация: счётчик из stats
            long statCnt=vc?(long)statsVals[3]:(long)statsVals[2];
            if(statCnt<0||cnt==statCnt){
                auto&flv=FoundFlags(c->name);
                if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
                for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
                g.saveCounts[c->name]=cnt;
                GTCLOGF("peritem: %ls stunts %d/%d (globals)",vc?L"VC":L"III",cnt,nJ);
            } else {
                GTCLOGF("peritem: stunts globals=%d stats=%ld -> счётчик",cnt,statCnt);
                if(statCnt>=0) MarkCountFromSave(*c,(int)statCnt);
            }
        }
    }
    // Rampages per-item
    if(Category* c=FindCatSub(L"Rampage")){
        int nR=vc?35:20;
        int varStart=vc?1439:862; // VC: $1439-$1473, III: $862-$881
        size_t rmpOff=gbase+(size_t)(varStart-1)*4;
        std::vector<int> fl((int)c->spots.size(),0);
        int cnt=0; bool valid=true;
        for(int i=0;i<nR&&valid;i++){
            size_t off=rmpOff+(size_t)i*4;
            if(off+4>b.size()){ valid=false; break; }
            uint32_t v=Rd32(b,off);
            if(v==0||v==1){ fl[i]=(int)v; cnt+=(int)v; }
            else valid=false;
        }
        if(valid&&cnt>0){
            long statCnt=vc?(long)statsVals[4]:(long)statsVals[3];
            GTCLOGF("peritem: rampage check cnt=%d statCnt=%ld",cnt,statCnt);
            if(statCnt<0||cnt==statCnt){
                auto&flv=FoundFlags(c->name);
                if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
                for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
                g.saveCounts[c->name]=cnt;
                GTCLOGF("peritem: %ls rampages %d/%d (globals)",vc?L"VC":L"III",cnt,nR);
            } else {
                GTCLOGF("peritem: rampages globals=%d stats=%ld -> счётчик",cnt,statCnt);
                if(statCnt>=0) MarkCountFromSave(*c,(int)statCnt);
            }
        }
    }
    // Store Robberies per-item (VC only)
    if(vc){
        if(Category* c=FindCatSub(L"Rober")){
            // $886-$888 (3) + $1544-$1555 (12) = 15 store robbery flags
            std::vector<int> fl((int)c->spots.size(),0);
            int cnt=0; bool valid=true;
            int idx=0;
            // $886-$888
            for(int n=886;n<=888&&valid;n++){
                uint32_t v=gvar(n);
                if(v==0||v==1){ if(idx<(int)fl.size()) fl[idx]=(int)v; cnt+=(int)v; idx++; }
                else valid=false;
            }
            // $1544-$1555
            for(int n=1544;n<=1555&&valid;n++){
                uint32_t v=gvar(n);
                if(v==0||v==1){ if(idx<(int)fl.size()) fl[idx]=(int)v; cnt+=(int)v; idx++; }
                else valid=false;
            }
            if(valid&&cnt>0&&idx>=15){
                // валидация: stats +0x140 (float)
                float statVal=0; size_t off=scrTag+16+887*4; // приблизительно
                // нет прямого stats offset здесь — используем >=0 проверку
                if(cnt>0&&cnt<=15){
                    auto&flv=FoundFlags(c->name);
                    if((int)flv.size()<(int)c->spots.size()) flv.resize(c->spots.size(),0);
                    for(size_t i=0;i<c->spots.size();i++) flv[i]=i<fl.size()?fl[i]:0;
                    g.saveCounts[c->name]=cnt;
                    GTCLOGF("peritem: VC robberies %d/15 (globals)",cnt);
                }
            }
        }
    }
}
