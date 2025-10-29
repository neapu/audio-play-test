//
// Created by liu86 on 2025/10/29.
//

#include "AudioRenderer.h"
#include "miniaudio/miniaudio.h"
#include "logger/logger.h"

AudioRenderer::AudioRenderer(const PullPcmDataFunc& pullFunc)
    : m_pullFunc(pullFunc)
{
}
AudioRenderer::~AudioRenderer()
{
}

bool AudioRenderer::start(int sampleRate, int channels)
{
    NEAPU_FUNC_TRACE;
    ma_device_config config;
    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = static_cast<ma_uint32>(channels);
    config.sampleRate = static_cast<ma_uint32>(sampleRate);
    config.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* renderer = static_cast<AudioRenderer*>(pDevice->pUserData);
        renderer->maDataCallback(pDevice, pOutput, pInput, frameCount);
    };
    config.pUserData = this;

    m_device = new ma_device;
    if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
        NEAPU_LOGE("Failed to initialize audio device");
        return false;
    }

    m_running.store(true);
    m_renderThread = std::thread(&AudioRenderer::renderThreadFunc, this);
    return true;
}

void AudioRenderer::stop()
{
    m_running = false;
    if (m_renderThread.joinable()) {
        m_renderThread.join();
    }

    if (m_device) {
        ma_device_uninit(m_device);
        delete m_device;
        m_device = nullptr;
    }
}

void AudioRenderer::renderThreadFunc() const
{
    NEAPU_FUNC_TRACE;
    if (ma_device_start(m_device) != MA_SUCCESS) {
        NEAPU_LOGE("Failed to start audio device");
        return;
    }
    NEAPU_LOGI("Audio device started");

    while (m_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ma_device_stop(m_device);
    NEAPU_LOGI("Audio device stopped");
}

void AudioRenderer::maDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, uint32_t frameCount)
{
    auto requireSize = static_cast<size_t>(frameCount) * 2 * sizeof(int16_t); // 2 channels, 16 bits
    size_t copyOffset = 0;
    while (copyOffset < requireSize) {
        updateCurrentData();
        if (!m_currentData) {
            // 数据拉取完毕，填充静音
            size_t remainSize = requireSize - copyOffset;
            std::memset(static_cast<uint8_t*>(pOutput) + copyOffset, 0, remainSize);
            m_playing = false;
            break;
        }
        m_playing = true;
        size_t availableSize = m_currentData->size - m_offset;
        size_t toCopy = std::min(availableSize, requireSize - copyOffset);
        std::memcpy(static_cast<uint8_t*>(pOutput) + copyOffset, m_currentData->data + m_offset, toCopy);
        m_offset += toCopy;
        copyOffset += toCopy;
    }

}

void AudioRenderer::updateCurrentData()
{
    if (!m_pullFunc) return;
    if (m_currentData && m_currentData->size == m_offset) {
        m_currentData.reset();
    }
    if (!m_currentData) {
        m_currentData = m_pullFunc();
        m_offset = 0;
    }
}