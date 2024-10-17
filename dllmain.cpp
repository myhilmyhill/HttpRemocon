#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"


// プラグインクラス
class CHttpRemocon : public TVTest::CTVTestPlugin
{
	bool m_fEnabled = false;

	static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void* pClientData);
	static CHttpRemocon* GetThis(HWND hwnd);

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

	return true;
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
