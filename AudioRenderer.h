//
// Created by liu86 on 2025/10/29.
//

#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include "pub.h"

using PullPcmDataFunc = std::function<PcmDataPtr()>;

typedef struct ma_device ma_device;

class AudioRenderer {
public:
    explicit AudioRenderer(const PullPcmDataFunc& pullFunc);
    ~AudioRenderer();

    bool start(int sampleRate, int channels);
    void stop();

    bool isPlaying() const { return m_playing.load(); }
private:
    void renderThreadFunc() const;
    void maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);

    void updateCurrentData();

private:
    PullPcmDataFunc m_pullFunc;
    ma_device* m_device{nullptr};

    std::thread m_renderThread;
    std::atomic_bool m_running{false};

    PcmDataPtr m_currentData{nullptr};
    size_t m_offset{0};

    std::atomic_bool m_playing{false};
};
