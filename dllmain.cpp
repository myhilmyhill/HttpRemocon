#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <thread>
#include <string>
#include <shlobj_core.h>
#include <filesystem>
#include <chrono>
#include <limits>
#include "httplib.h"

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"

static const char* defaultHost = "0.0.0.0";
static const char* allowOrigin = "*";
static const int defaultPort = 8080;
static const char delimiter = ',';
static const UINT WM_TVTP_APP = 0x8000;
static const UINT WM_TVTP_GET_POSITION = WM_TVTP_APP + 52;
static const UINT WM_TVTP_IS_PAUSED = WM_TVTP_APP + 56;
static const UINT WM_TVTP_GET_STRETCH = WM_TVTP_APP + 58;
static const UINT WM_TVTP_SEEK = WM_TVTP_APP + 60;
static const UINT WM_TVTP_SEEK_ABSOLUTE = WM_TVTP_APP + 61;

static void PrintChannel(std::ostringstream& output, const WCHAR* szDriver, const TVTest::ChannelInfo& ch);
std::wstring convertUtf8ToWstring(const std::string& utf8);
std::string convertWstringToUtf8(const std::wstring& wstr);
static void SimulateDropFiles(HWND hwndTarget, const std::wstring& filePath);
static std::string WideCharToUTF8(const WCHAR* pWideChar);
static int ParseTimeToMilliseconds(const std::string& input);
std::filesystem::path findRecentBMPFile(const std::wstring& directory, const std::chrono::system_clock::time_point& lastSaveTime);
std::vector<char> readFile(const std::filesystem::path& filePath);

// プラグインクラス
class CHttpRemocon : public TVTest::CTVTestPlugin
{
    bool m_fEnabled = false;
    httplib::Server m_server;
    std::thread m_serverThread;

    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void* pClientData);
    static CHttpRemocon* GetThis(HWND hwnd);
    void StartHttpServer();
    void StopHttpServer();
    std::string GetTunerList();
    void SetChannel(const std::string& body, httplib::Response& res);

public:
    bool GetPluginInfo(TVTest::PluginInfo* pInfo) override;
    bool Initialize() override;
    bool Finalize() override;
};


bool CHttpRemocon::GetPluginInfo(TVTest::PluginInfo* pInfo)
{
    // プラグインの情報を返す
    pInfo->Type = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags = TVTest::PLUGIN_FLAG_NONE;
    pInfo->pszPluginName = L"HttpRemocon";
    return true;
}


bool CHttpRemocon::Initialize()
{
    // 初期化処理

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

    return true;
}


bool CHttpRemocon::Finalize()
{
    // 終了処理
    if (m_fEnabled) {
        StopHttpServer();
    }

    return true;
}


void CHttpRemocon::StartHttpServer()
{
    // サーバが実行されていない場合にスレッドを開始
    if (m_serverThread.joinable()) {
        return;  // サーバがすでに起動中の場合は何もしない
    }

    m_serverThread = std::thread([this]() {
        m_server.Get("/", [this](const httplib::Request& req, httplib::Response& res) {
            res.status = 200;
            });

        m_server.Post("/", [this](const httplib::Request& req, httplib::Response& res) {
            if (req.body == "close") {
                m_pApp->Close();
                res.status = 200;
            }
            else if (req.body == "sleep") {
                // レスポンスを返すためスリープ処理を別スレッドで実行
                std::thread([this]() {
                    m_pApp->SetDriverName(nullptr);

                    // 画面オフにならずモダンスタンバイになるらしい
                    SendNotifyMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
                    }).detach();

                res.status = 200;
            }
            else {
                res.status = 400;
                res.set_content("Invalid operation value", "text/plain");
            }
            });

        m_server.Post("/play", [this](const httplib::Request& req, httplib::Response& res) {
            std::wstring filePath = convertUtf8ToWstring(req.body);

            // /tvtpipe はすでにあるものとみなす
            if (!m_pApp->SetDriverName(L"BonDriver_Pipe.dll")) {
                res.status = 500;
                res.set_content("Failed SetDriverName", "text/plain");
                return;
            }
            // ServiceId が正常に 0 なのにエラー発生が返る。エラーチェックはしない
            m_pApp->SetChannel(0, 0);

            // ドラッグアンドドロップとしてファイルを開く
            HWND hwndTarget = FindWindow(L"TVTest Window", NULL);
            if (hwndTarget) {
                SimulateDropFiles(hwndTarget, filePath);
            }

            res.status = 200;
            });

        m_server.Get("/play/pause", [this](const httplib::Request& req, httplib::Response& res) {
            HWND hwnd = FindWindow(TEXT("TvtPlay Frame"), NULL);
            if (hwnd == NULL) {
                res.status = 500;
                res.set_content("Failed FindWindow: TvtPlay Frame", "text/plain");
                return;
            }

            bool paused = SendMessage(hwnd, WM_TVTP_IS_PAUSED, 0, 0);
            res.status = 200;
            res.set_content(std::to_string(paused), "text/plain");
            });

        m_server.Post("/play/pause", [this](const httplib::Request& req, httplib::Response& res) {
            // トグルしかできないので body は見ない
            if (!m_pApp->DoCommand(L"tvtplay.tvtp:Pause")) {
                res.status = 500;
                res.set_content("Failed DoCommand: tvtplay.tvtp:Pause", "text/plain");
                return;
            }
            res.status = 200;
            });

        m_server.Get("/play/pos", [this](const httplib::Request& req, httplib::Response& res) {
            HWND hwnd = FindWindow(TEXT("TvtPlay Frame"), NULL);
            if (hwnd == NULL) {
                res.status = 500;
                res.set_content("Failed FindWindow: TvtPlay Frame", "text/plain");
                return;
            }

            int pos = SendMessage(hwnd, WM_TVTP_GET_POSITION, 0, 0);
            res.status = 200;
            res.set_content(std::to_string(pos), "text/plain");
            });

        m_server.Post("/play/pos", [this](const httplib::Request& req, httplib::Response& res) {
            HWND hwnd = FindWindow(TEXT("TvtPlay Frame"), NULL);
            if (hwnd == NULL) {
                res.status = 500;
                res.set_content("Failed FindWindow: TvtPlay Frame", "text/plain");
                return;
            }

            const auto& command = req.body;
            char sign = command[0] == '-' || command[0] == '+';
            int msec = ParseTimeToMilliseconds(sign ? command.substr(1) : command);

            if (sign || command.find(':') == std::string::npos) {
                msec = command[0] == '-' ? -msec : msec;
                SendMessage(hwnd, WM_TVTP_SEEK, 0, (LPARAM)msec);
            }
            else {
                SendMessage(hwnd, WM_TVTP_SEEK_ABSOLUTE, 0, (LPARAM)msec);
            }

            // 現在時刻への反映に時間がかかるので返すのはやめる
            res.status = 200;
            });

        m_server.Get("/play/speed", [this](const httplib::Request& req, httplib::Response& res) {
            HWND hwnd = FindWindow(TEXT("TvtPlay Frame"), NULL);
            if (hwnd == NULL) {
                res.status = 500;
                res.set_content("Failed FindWindow: TvtPlay Frame", "text/plain");
                return;
            }

            int stretch = HIWORD(SendMessage(hwnd, WM_TVTP_GET_STRETCH, 0, 0));
            res.status = 200;
            res.set_content(std::to_string(stretch), "text/plain");
            });

        m_server.Post("/play/speed", [this](const httplib::Request& req, httplib::Response& res) {
            // TvtPlay を見てもあんまり柔軟なことはできなそう
            std::wstring command = L"tvtplay.tvtp:Stretch";
            if (req.body.length() == 1 && 'A' <= req.body[0] && req.body[0] <= 'Z') {
                command += req.body[0];
            }

            if (!m_pApp->DoCommand(command.c_str())) {
                res.status = 500;
                std::string error_message = "Failed DoCommand: " + convertWstringToUtf8(command);
                res.set_content(error_message, "text/plain");
                return;
            }

            res.status = 200;
            });

        m_server.Get("/vol", [this](const httplib::Request& req, httplib::Response& res) {
            int vol = m_pApp->GetVolume();
            res.set_content(std::to_string(vol), "text/plain");
            res.status = 200;
            });

        m_server.Post("/vol", [this](const httplib::Request& req, httplib::Response& res) {
            int volumeChange = std::stoi(req.body);
            int currentVolume = m_pApp->GetVolume();

            if (req.body[0] == '+' || req.body[0] == '-') {
                // 相対値として設定
                currentVolume += volumeChange;
            }
            else {
                // 絶対値として設定
                currentVolume = volumeChange;
            }

            // 音量が範囲内に収まるように制限
            if (currentVolume < 0) currentVolume = 0;
            if (currentVolume > 100) currentVolume = 100;

            if (!m_pApp->SetVolume(currentVolume)) {
                res.status = 500;
                res.set_content("Failed SetVolume", "text/plain");
                return;
            }
            res.set_content(std::to_string(currentVolume), "text/plain");

            res.status = 200;
            });

        m_server.Get("/ch", [this](const httplib::Request& req, httplib::Response& res) {
            res.set_content(GetTunerList(), "text/plain");
            res.status = 200;
            });

        m_server.Post("/ch", [this](const httplib::Request& req, httplib::Response& res) {
            SetChannel(req.body, res);
            });

        m_server.Get("/rec", [this](const httplib::Request& req, httplib::Response& res) {
            TVTest::RecordStatusInfo status = {};
            m_pApp->GetRecordStatus(&status);

            res.status = 200;
            switch (status.Status) {
            case TVTest::RECORD_STATUS_NOTRECORDING:
                res.set_content("Not recording", "text/plain");
                return;

            case TVTest::RECORD_STATUS_RECORDING:
                res.set_content("Recording", "text/plain");
                return;

            case TVTest::RECORD_STATUS_PAUSED:
                res.set_content("Paused", "text/plain");
                return;

            default:
                res.status = 500;
                res.set_content("Invalid status", "text/plain");
                return;
            }
            });

        m_server.Post("/rec", [this](const httplib::Request& req, httplib::Response& res) {
            TVTest::RecordStatusInfo status = {};

            if (req.body == "start") {
                m_pApp->GetRecordStatus(&status);
                if (status.Status == TVTest::RECORD_STATUS_RECORDING) {
                    res.status = 400;
                    res.set_content("Already start recording", "text/plain");
                    return;
                }

                if (!m_pApp->DoCommand(L"TimeShiftRecording")) {
                    res.status = 500;
                    res.set_content("Failed DoCommand: TimeShiftRecording", "text/plain");
                    return;
                }

                if (!m_pApp->DoCommand(L"RecordEvent")) {
                    res.status = 500;
                    res.set_content("Failed DoCommand: RecordEvent", "text/plain");
                    return;
                }

                // 録画ファイル名を取得
                WCHAR fileName[MAX_PATH] = {};
                status.pszFileName = fileName;
                status.MaxFileName = MAX_PATH;
                m_pApp->GetRecordStatus(&status);

                res.set_content(WideCharToUTF8(fileName), "text/plain");
                res.status = 200;
            }
            else if (req.body == "stop") {

                // 録画ファイル名を取得
                WCHAR fileName[MAX_PATH] = {};
                status.pszFileName = fileName;
                status.MaxFileName = MAX_PATH;
                m_pApp->GetRecordStatus(&status);

                if (status.Status != TVTest::RECORD_STATUS_RECORDING) {
                    res.status = 400;
                    res.set_content("Not yet started recording", "text/plain");
                    return;
                }

                if (!m_pApp->StopRecord()) {
                    res.status = 500;
                    res.set_content("Failed StopRecord", "text/plain");
                    return;
                }

                res.set_content(WideCharToUTF8(fileName), "text/plain");
                res.status = 200;
            }
            else {
                res.status = 400;
                res.set_content("Invalid operation value", "text/plain");
            }
            });

        m_server.Post("/view/cap", [this](const httplib::Request& req, httplib::Response& res) {
            // たぶん保存したキャプチャのファイル名がわからない。
            // ファイルのタイムスタンプで頑張って探す。
            // CaptureImageをした後にSaveImageは時間差ができるのでダメだった
            auto saveStartTime = std::chrono::system_clock::now();

            auto saved = m_pApp->SaveImage();
            if (!saved) {
                res.status = 500;
                res.set_content("Failed SaveImage", "text/plain");
                return;
            }

            // 相対パスの場合、あきらめる
            WCHAR szFolder[MAX_PATH] = {};
            if (m_pApp->GetSetting(L"CaptureFolder", szFolder, MAX_PATH) < 1) {
                res.status = 500;
                res.set_content("Failed GetSetting; CaptureFolder", "text/plain");
            }

            auto bmpFilePath = findRecentBMPFile(std::wstring(szFolder), saveStartTime);
            if (bmpFilePath.empty()) {
                res.status = 500;
                res.set_content("Invalid bmp file path", "text/plain");
                return;
            }

            auto bmpData = readFile(bmpFilePath);
            if (bmpData.empty()) {
                res.status = 500;
                res.set_content("Failed to read bmp file", "text/plain");
                return;
            }

            res.set_content(bmpData.data(), bmpData.size(), "image/bmp");
            res.status = 200;
            });

        m_server.Post("/view/panel", [this](const httplib::Request& req, httplib::Response& res) {
            // トグルしかできないので body は見ない
            if (!m_pApp->DoCommand(L"Panel")) {
                res.status = 500;
                res.set_content("Failed DoCommand: Panel", "text/plain");
                return;
            }

            res.status = 200;
            });

        m_server.Post("/view/reset", [this](const httplib::Request& req, httplib::Response& res) {
            if (!m_pApp->Reset(TVTest::RESET_VIEWER)) {
                res.status = 500;
                res.set_content("Failed Reset", "text/plain");
                return;
            }

            res.status = 200;
            });

        m_server.set_exception_handler([](const auto& req, auto& res, std::exception_ptr ep) {
            auto fmt = "%s";
            char buf[BUFSIZ];
            try {
                std::rethrow_exception(ep);
            }
            catch (std::exception& e) {
                snprintf(buf, sizeof(buf), fmt, e.what());
            }
            catch (...) {
                snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            }
            res.set_content(buf, "text/plain");
            res.status = 500;
            });

        m_server.set_default_headers({
            { "Access-Control-Allow-Origin", allowOrigin },
            });
        m_server.listen(defaultHost, defaultPort);
        });
}


void PrintChannel(std::ostringstream& output, const WCHAR* szDriver, const TVTest::ChannelInfo& ch) {
    std::string sDriver = WideCharToUTF8(szDriver);
    std::string sChannelName = WideCharToUTF8(ch.szChannelName);

    output << sDriver << delimiter
        << ch.Space << delimiter
        << ch.Channel << delimiter
        << ch.ServiceID << ": "
        << sChannelName << "\n";
}

// チューナー/チャンネルのリストを取得する
std::string CHttpRemocon::GetTunerList()
{
    std::ostringstream tunerList;
    WCHAR szDriver[MAX_PATH];

    {
        TVTest::ChannelInfo ch;
        m_pApp->GetCurrentChannelInfo(&ch);
        m_pApp->GetDriverName(szDriver, _countof(szDriver));
        PrintChannel(tunerList, szDriver, ch);
        tunerList << "\n";
    }
    {
        for (int i = 0; m_pApp->EnumDriver(i, szDriver, _countof(szDriver)) > 0; i++) {
            TVTest::DriverTuningSpaceList spaces;
            if (m_pApp->GetDriverTuningSpaceList(szDriver, &spaces)) {
                for (DWORD j = 0; j < spaces.NumSpaces; j++) {
                    const TVTest::DriverTuningSpaceInfo& chs = *spaces.SpaceList[j];
                    for (DWORD k = 0; k < chs.NumChannels; k++) {
                        const TVTest::ChannelInfo& ch = *chs.ChannelList[k];
                        if (!(ch.Flags & TVTest::CHANNEL_FLAG_DISABLED)) {
                            PrintChannel(tunerList, szDriver, ch);
                        }
                    }
                }

                m_pApp->FreeDriverTuningSpaceList(&spaces);
            }
        }
    }

    return tunerList.str();
}


void CHttpRemocon::SetChannel(const std::string& body, httplib::Response& res) {
    TVTest::ChannelSelectInfo info = {};
    info.Size = sizeof(info);
    info.pszTuner = nullptr;
    info.Space = -1;
    info.Channel = -1;
    info.ServiceID = 0;

    // req.body は "pszTuner,Channel,ServiceID" の形式
    std::istringstream iss(body);
    std::string tuner, space, channelStr, serviceIdStr;
    std::wstring wTuner;

    if (std::getline(iss, tuner, delimiter) && std::getline(iss, space, delimiter) && std::getline(iss, channelStr, delimiter) && std::getline(iss, serviceIdStr)) {
        if (!tuner.empty()) {
            wTuner = std::wstring(tuner.begin(), tuner.end());
            info.pszTuner = wTuner.c_str();
        }

        if (!channelStr.empty()) {
            try {
                info.Channel = std::stoi(channelStr);
            }
            catch (const std::invalid_argument&) {
                res.status = 400;
                res.set_content("Invalid Channel value", "text/plain");
                return;
            }
        }

        if (!space.empty()) {
            try {
                info.Space = std::stoi(space);
            }
            catch (const std::invalid_argument&) {
                res.status = 400;
                res.set_content("Invalid Space value", "text/plain");
                return;
            }
        }

        if (!serviceIdStr.empty()) {
            try {
                info.ServiceID = std::stoi(serviceIdStr);
            }
            catch (const std::invalid_argument&) {
                res.status = 400;
                res.set_content("Invalid ServiceID value", "text/plain");
                return;
            }
        }

        // チャンネル選択
        if (!m_pApp->SelectChannel(&info)) {
            res.status = 500;
            res.set_content("Failed SelectChannel", "text/plain");
            return;
        }

        res.status = 200;
    }
    else {
        // パースエラー
        res.status = 400;
        res.set_content("Invalid request format", "text/plain");
    }
}


void CHttpRemocon::StopHttpServer()
{
    if (m_server.is_running()) {
        m_server.stop();
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();  // サーバスレッドの終了を待機
    }
}

// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CHttpRemocon::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void* pClientData)
{
    CHttpRemocon* pThis = static_cast<CHttpRemocon*>(pClientData);

    switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        pThis->m_fEnabled = lParam1 != 0;
        if (pThis->m_fEnabled) {
            pThis->StartHttpServer();
        }
        else {
            pThis->StopHttpServer();
        }
        return TRUE;
    }

    return 0;
}


CHttpRemocon* CHttpRemocon::GetThis(HWND hwnd)
{
    return reinterpret_cast<CHttpRemocon*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
}


TVTest::CTVTestPlugin* CreatePluginClass()
{
    return new CHttpRemocon;
}

// UTF-8 から UTF-16 への変換
std::wstring convertUtf8ToWstring(const std::string& utf8) {
    int wideCharSize = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wideCharSize == 0) {
        throw std::runtime_error("Failed to convert UTF-8 to UTF-16");
    }

    std::wstring wstr(wideCharSize, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], wideCharSize);
    return wstr;
}

// UTF-16 から UTF-8 への変換
std::string convertWstringToUtf8(const std::wstring& wstr) {
    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size == 0) {
        throw std::runtime_error("Failed to convert UTF-16 to UTF-8");
    }

    std::string utf8(utf8Size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], utf8Size, nullptr, nullptr);
    return utf8;
}

// ファイルのドラッグアンドドロップをシミュレートする関数
void SimulateDropFiles(HWND hwndTarget, const std::wstring& filePath)
{
    // ドロップするファイルのリストを準備
    DROPFILES dropFiles = { 0 };
    dropFiles.pFiles = sizeof(DROPFILES);  // ファイルリストのオフセット
    dropFiles.fNC = TRUE;                  // 非クライアントエリアのフラグ
    dropFiles.pt.x = 0;                    // ドロップ位置（相対座標）
    dropFiles.pt.y = 0;
    dropFiles.fWide = TRUE;

    // ファイルパスを二重終端で準備（必要な形式）
    size_t filePathSize = (filePath.length() + 1) * sizeof(wchar_t);
    size_t totalSize = sizeof(DROPFILES) + filePathSize;

    // メモリを確保
    HGLOBAL hGlobal = GlobalAlloc(GHND, totalSize);
    if (hGlobal) {
        // メモリをロックしてアクセス可能にする
        BYTE* pData = (BYTE*)GlobalLock(hGlobal);
        if (pData) {
            // DROPFILES構造体をコピー
            memcpy(pData, &dropFiles, sizeof(DROPFILES));

            // ファイルパスをコピー（DROPFILES構造体の直後に）
            memcpy(pData + sizeof(DROPFILES), filePath.c_str(), filePathSize);

            // メモリをアンロック
            GlobalUnlock(hGlobal);

            // ターゲットウィンドウにWM_DROPFILESメッセージを送信
            PostMessage(hwndTarget, WM_DROPFILES, (WPARAM)hGlobal, 0);
        }
        else {
            // メモリのロックに失敗した場合
            GlobalFree(hGlobal);
        }
    }
}


std::string WideCharToUTF8(const WCHAR* pWideChar)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, pWideChar, -1, nullptr, 0, nullptr, nullptr);
    if (len == 0) {
        return "";
    }
    std::string utf8Str(len - 1, '\0'); // 終端文字分を引く
    WideCharToMultiByte(CP_UTF8, 0, pWideChar, -1, &utf8Str[0], len, nullptr, nullptr);
    return utf8Str;
}

int ParseTimeToMilliseconds(const std::string& input) {
    int hours = 0, minutes = 0, seconds = 0;
    char _;

    std::istringstream iss(input);
    auto count = std::count(input.begin(), input.end(), ':');
    if (count == 0) {
        // 秒のみの場合 (例: "1")
        iss >> seconds;
    }
    else if (count == 1) {
        // 分:秒 の場合 (例: "0:10")
        iss >> minutes >> _ >> seconds;
    }
    else if (count == 2) {
        // 時:分:秒 の場合 (例: "1:00:00")
        iss >> hours >> _ >> minutes >> _ >> seconds;
    }
    else {
        throw std::invalid_argument("Invalid time format");
    }

    return (hours * 3600 + minutes * 60 + seconds) * 1000;
}

// 指定された時間範囲内のBMPファイルを探す関数
std::filesystem::path findRecentBMPFile(const std::wstring& directory, const std::chrono::system_clock::time_point& lastSaveTime) {
    auto lastSaveFileTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        std::filesystem::file_time_type::clock::now() - (std::chrono::system_clock::now() - lastSaveTime));

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bmp") {
            auto lastWriteTime = std::filesystem::last_write_time(entry);

            if (lastWriteTime >= lastSaveFileTime) {
                return entry.path();
            }
        }
    }
    return std::filesystem::path{};
}

// https://coniferproductions.com/posts/2022/10/25/reading-binary-files-cpp/
std::vector<char> readFile(const std::filesystem::path& filePath) {
    auto lengthUintmax = std::filesystem::file_size(filePath);
    if (lengthUintmax == 0) return std::vector<char>{};
    if (lengthUintmax > std::numeric_limits<size_t>::max()) throw std::overflow_error("Too large file");

    size_t length = static_cast<size_t>(lengthUintmax);
    std::vector<char> buffer(length);
    std::ifstream inputFile(filePath, std::ios::binary);
    if (!inputFile) throw std::runtime_error("Failed to open file");
    if (!inputFile.read(buffer.data(), length)) throw std::runtime_error("Failed to read file");

    return buffer;
}
