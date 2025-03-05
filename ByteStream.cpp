#pragma once

#include <vector>
#include <cstdint>
#include <queue>
#include <mutex>
#include "LibISDB/LibISDB/LibISDB.hpp"
#include "LibISDB/LibISDB/Base/Stream.hpp"

using namespace LibISDB;

class ByteStream : public Stream {
private:
    std::queue<uint8_t> m_Buffer;
    size_t m_MaxSize;
    mutable std::mutex m_Mutex;

public:
    explicit ByteStream(size_t maxSize = 1024 * 1024) // デフォルトで約1MB分のBYTEを保持
        : m_MaxSize(maxSize) {}

    ~ByteStream() {
        Close();
    }

    bool Close() override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::queue<uint8_t>().swap(m_Buffer); // バッファをクリア
        return true;
    }

    bool IsOpen() const override {
        return true; // 常にオープン状態とする
    }

    size_t Read(void* pBuff, size_t Size) override {
        if (!pBuff || Size == 0) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        size_t actualRead = 0;
        uint8_t* out = static_cast<uint8_t*>(pBuff);

        while (!m_Buffer.empty() && actualRead < Size) {
            out[actualRead++] = m_Buffer.front();
            m_Buffer.pop();
        }

        return actualRead;
    }

    size_t Write(const void* pBuff, size_t Size) override {
        if (!pBuff || Size == 0) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_Mutex);
        const uint8_t* data = static_cast<const uint8_t*>(pBuff);

        for (size_t i = 0; i < Size; ++i) {
            if (m_Buffer.size() >= m_MaxSize) {
                m_Buffer.pop(); // 古いデータを削除
            }
            m_Buffer.push(data[i]); // バッファの末尾に追加
        }

        return Size;
    }

    bool Flush() override {
        return true; // 特に処理なし
    }

    SizeType GetSize() override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Buffer.size();
    }

    OffsetType GetPos() override {
        return 0; // stdin のように現在位置を取得できないとする
    }

    bool SetPos(OffsetType Pos, SetPosType Type = SetPosType::Begin) override {
        return false; // シーク不可
    }

    bool IsEnd() const override {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Buffer.empty();
    }
};
