#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <thread>
#include <string>
#include <codecvt>
#include <shlobj_core.h>
#include "httplib.h"

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"

static const char* defaultHost = "0.0.0.0";
static const int defaultPort = 8080;
static const char delimiter = ',';
static void PrintChannel(std::ostringstream& output, const WCHAR* szDriver, const TVTest::ChannelInfo& ch);
static void SimulateDropFiles(HWND hwndTarget, const std::wstring& filePath);
static std::string WideCharToUTF8(const WCHAR* pWideChar);

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
                // スリープ処理を別スレッドで実行
                std::thread([this]() {
                    // Windowsをスリープさせる
                    HANDLE hToken;
                    TOKEN_PRIVILEGES tp;

                    // プロセスのトークンを取得
                    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);

                    // SE_SHUTDOWN_NAME権限を取得
                    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
                    tp.PrivilegeCount = 1;
                    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                    // 権限を調整
                    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);

                    // スリープを実行
                    SetSystemPowerState(TRUE, FALSE);
                    }).detach();  // スレッドをデタッチして即座に戻る

                res.status = 200;
            }
            else {
                res.status = 400;
                res.set_content("Invalid operation value", "text/plain");
            }
            });

        m_server.Post("/play", [this](const httplib::Request& req, httplib::Response& res) {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            std::wstring filePath = converter.from_bytes(req.body);

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
                        PrintChannel(tunerList, szDriver, ch);
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
    info.Space = -1;
    info.Channel = -1;
    info.ServiceID = 0;

    // req.body は "pszTuner,Channel,ServiceID" の形式
    std::istringstream iss(body);
    std::string tuner, space, channelStr, serviceIdStr;

    if (std::getline(iss, tuner, delimiter) && std::getline(iss, space, delimiter) && std::getline(iss, channelStr, delimiter) && std::getline(iss, serviceIdStr)) {
        if (!tuner.empty()) {
            std::wstring wTuner = std::wstring(tuner.begin(), tuner.end());
            info.pszTuner = wTuner.c_str();
        }

        if (!channelStr.empty()) {
            try {
                info.Channel = std::stoi(channelStr);
            }
            catch (const std::invalid_argument& e) {
                res.status = 400;
                res.set_content("Invalid Channel value", "text/plain");
                return;
            }
        }

        if (!space.empty()) {
            try {
                info.Space = std::stoi(space);
            }
            catch (const std::invalid_argument& e) {
                res.status = 400;
                res.set_content("Invalid Space value", "text/plain");
                return;
            }
        }

        if (!serviceIdStr.empty()) {
            try {
                info.ServiceID = std::stoi(serviceIdStr);
            }
            catch (const std::invalid_argument& e) {
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
