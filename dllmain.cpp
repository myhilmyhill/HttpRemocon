#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <thread>
#include <string>
#include <codecvt>
#include <shlobj_core.h>
#include "httplib.h"

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"

void SimulateDropFiles(HWND hwndTarget, const std::wstring& filePath);

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

        m_server.Post("/play", [this](const httplib::Request& req, httplib::Response& res) {
            std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
            std::wstring filePath = converter.from_bytes(req.body);

            // /tvtpipe はすでにあるものとみなす
            m_pApp->SetDriverName(L"BonDriver_Pipe.dll");
            m_pApp->SetChannel(0, 0);

            // ドラッグアンドドロップとしてファイルを開く
            HWND hwndTarget = FindWindow(L"TVTest Window", NULL);
            if (hwndTarget) {
                SimulateDropFiles(hwndTarget, filePath);
            }

            res.status = 200;
            });

        m_server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
            res.status = 404;
            });

        m_server.listen("0.0.0.0", 8080);
        });
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
