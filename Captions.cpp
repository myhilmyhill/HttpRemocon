#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "LibISDB/LibISDB/LibISDB.hpp"
#include "LibISDB/LibISDB/Base/StandardStream.hpp"
#include "LibISDB/LibISDB/Engine/FilterGraph.hpp"
#include "LibISDB/LibISDB/Engine/TSEngine.hpp"
#include "LibISDB/LibISDB/Filters/AnalyzerFilter.hpp"
#include "LibISDB/LibISDB/Filters/CaptionFilter.hpp"
#include "LibISDB/LibISDB/Filters/FilterBase.hpp"
#include "LibISDB/LibISDB/Filters/StreamSourceFilter.hpp"
#include "LibISDB/LibISDB/Filters/TSPacketParserFilter.hpp"
#include "ByteStream.cpp"
#include "Engine.cpp"

using namespace LibISDB;

class Captions {
    ByteStream* Stream = nullptr;
    Engine Engine;
    AnalyzerFilter* Analyzer = nullptr;

    class : public CaptionFilter::Handler {
    private:
        std::wstringstream wss;
        bool fClearLast = false;
        bool fContinue = false;
        static const bool m_fIgnoreSmall = true;

    public:
        void OnLanguageUpdate(CaptionFilter* pFilter, CaptionParser* pParser) {}
        void OnCaption(
            CaptionFilter* pFilter, CaptionParser* pParser,
            uint8_t Language, const CharType* pText,
            const ARIBStringDecoder::FormatList* pFormatList) {
#ifdef _DEBUG
            OutputDebugStringW(pText);
#endif
            const int Length = ::lstrlen(pText);

            if (Length > 0) {

                int i;
                for (i = 0; i < Length; i++) {
                    if (pText[i] != '\f')
                        break;
                }
                if (i == Length) {
                    if (fClearLast || fContinue)
                        return;
                    fClearLast = true;
                }
                else {
                    fClearLast = false;
                }

                std::wstring Buff(pText);

                if (m_fIgnoreSmall && !pParser->Is1Seg()) {
                    for (int i = static_cast<int>(pFormatList->size()) - 1; i >= 0; i--) {
                        if ((*pFormatList)[i].Size == ARIBStringDecoder::CharSize::Small) {
                            const size_t Pos = (*pFormatList)[i].Pos;
                            if (Pos < Buff.length()) {
                                if (i + 1 < static_cast<int>(pFormatList->size())) {
                                    const size_t NextPos = std::min(Buff.length(), (*pFormatList)[i + 1].Pos);
                                    //TRACE(TEXT("Caption exclude : {}\n"), StringView(&Buff[Pos], NextPos - Pos));
                                    Buff.erase(Pos, NextPos - Pos);
                                }
                                else {
                                    Buff.erase(Pos);
                                }
                            }
                        }
                    }
                }

                for (size_t i = 0; i < Buff.length(); i++) {
                    if (Buff[i] == '\f') {
                        if (i == 0 && !fContinue) {
                            Buff.replace(0, 1, TEXT("\n"));
                            i++;
                        }
                        else {
                            Buff.erase(i, 1);
                        }
                    }
                }
                fContinue =
                    Buff.length() > 1 && Buff.back() == L'→';
                if (fContinue)
                    Buff.pop_back();
                if (!Buff.empty()) {
                    wss << Buff;
                }
            }
        }
        std::wstring GetStockedCaptions() { return wss.str(); }
        void ClearStockedCaptions() { wss.str(L""); wss.clear(); }
        void InitCaptions(std::wstring captions) {
            wss.str(captions);
            wss.clear(); // ストリーム状態をリセット
            wss.seekp(0, std::ios::end); // 追記位置を末尾に移動
        }
    } CaptionHandler;

public:
    Captions(std::wstring captions = L"") {
        // 渡した先で unique_ptr として登録されるので、 delete しない
        auto Source = new StreamSourceFilter;
        auto Parser = new TSPacketParserFilter;
        auto Analyzer = this->Analyzer = new AnalyzerFilter;
        auto Caption = new CaptionFilter;
        Stream = new ByteStream;

        Engine.BuildEngine({
            Source,
            Parser,
            Analyzer,
            Caption,
            });
        Caption->SetCaptionHandler(&CaptionHandler);
        Engine.SetStartStreamingOnSourceOpen(true);
        Engine.OpenSource(Stream);

        CaptionHandler.InitCaptions(captions);
    }

    static BOOL CALLBACK StreamCallback(BYTE* pData, void* pClientData) {
        auto pThis = static_cast<Captions*>(pClientData);
        if (pThis->Stream) pThis->Stream->Write(pData, 188);
        return TRUE;
    }
    std::wstring GetStockedCaptions() { return CaptionHandler.GetStockedCaptions(); }
    void ClearStockedCaptions() { CaptionHandler.ClearStockedCaptions(); }
    std::string GetTOTTime() {
        LibISDB::DateTime time;
        if (Analyzer->GetInterpolatedTOTTime(&time)) {
            char buffer[std::size("yyyy-mm-ddThh:mm:ss+09:00")] = {};
            const std::tm tm = time.ToTm();
            std::strftime(std::data(buffer), std::size(buffer), "%FT%T+09:00", &tm);
            return std::string(buffer);
        }
        return std::string();
    }
};
