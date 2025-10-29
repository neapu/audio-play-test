//
// Created by liu86 on 2025/10/29.
//

#include "Decoder.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
#include "logger/logger.h"

static double dataDuration(const PcmDataPtr& data, int sampleRate, int channels)
{
    // 计算一份数据可以播放的时长（秒）
    if (!data || sampleRate <= 0 || channels <= 0) {
        return 0.0;
    }

    return data->nbSamples / static_cast<double>(sampleRate * channels);
}

PcmData::~PcmData()
{
    delete[] data;
}

bool Decoder::open(const std::string& url)
{
    NEAPU_FUNC_TRACE;
    if (m_formatCtx) {
        NEAPU_LOGE("Decoder already opened");
        return false;
    }

    m_isDecodeFinished = false;
    int ret = avformat_open_input(&m_formatCtx, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        NEAPU_LOGE("Failed to open media: {}", url);
        return false;
    }

    ret = avformat_find_stream_info(m_formatCtx, nullptr);
    if (ret < 0) {
        NEAPU_LOGE("Failed to find stream info");
        close();
        return false;
    }

    const AVCodec* codec = nullptr;
    m_audioStreamIndex = av_find_best_stream(m_formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (m_audioStreamIndex < 0) {
        NEAPU_LOGE("No audio stream found");
        close();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        NEAPU_LOGE("Failed to allocate codec context");
        close();
        return false;
    }

    ret = avcodec_parameters_to_context(m_codecCtx, m_formatCtx->streams[m_audioStreamIndex]->codecpar);
    if (ret < 0) {
        NEAPU_LOGE("Failed to copy codec parameters to context");
        close();
        return false;
    }

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        NEAPU_LOGE("Failed to open codec");
        close();
        return false;
    }

    m_mediaSampleRate = m_codecCtx->sample_rate;
    m_mediaChannels = m_codecCtx->ch_layout.nb_channels;

    if (!initSwrContext()) {
        NEAPU_LOGE("Failed to initialize SwrContext");
        close();
        return false;
    }

    m_packet = av_packet_alloc();
    if (!m_packet) {
        NEAPU_LOGE("Failed to allocate AVPacket");
        close();
        return false;
    }

    m_frame = av_frame_alloc();
    if (!m_frame) {
        NEAPU_LOGE("Failed to allocate AVFrame");
        close();
        return false;
    }

    m_threadsRunning = true;
    m_decodeThread = std::thread(&Decoder::decodeThreadFunc, this);

    return true;
}

void Decoder::close()
{
    m_threadsRunning = false;
    m_queueCondVar.notify_all();
    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    {
        std::lock_guard lock(m_queueMutex);
        while (!m_pcmQueue.empty()) {
            m_pcmQueue.pop();
        }
    }

    if (m_swrCtx) {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }

    if (m_dstData) {
        av_freep(&m_dstData[0]);
        av_freep(&m_dstData);
        m_dstData = nullptr;
        m_dstNbSamples = 0;
        m_dstLineSize = 0;
    }

    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }

    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }
}

int Decoder::mediaSampleRate() const
{
    return m_mediaSampleRate;
}

int Decoder::mediaChannels() const
{
    return m_mediaChannels;
}

PcmDataPtr Decoder::pullPcmData()
{
    std::lock_guard lock(m_queueMutex);
    if (m_pcmQueue.empty()) {
        return nullptr;
    }
    auto pcmData = std::move(m_pcmQueue.front());
    m_pcmQueue.pop();
    m_waterLevel -= dataDuration(pcmData, m_mediaSampleRate, m_mediaChannels);
    m_queueCondVar.notify_all();
    return pcmData;
}

void Decoder::decodeThreadFunc()
{
    while (m_threadsRunning) {
        if (!readFrame()) {
            break;
        }

        if (!sendPacket()) {
            // 非音频包或发送失败，释放并继续
            av_packet_unref(m_packet);
            continue;
        }

        while (recvFrame()) {
            processFrame();
        }

        // 统一在处理完成后释放当前包
        av_packet_unref(m_packet);
    }
    m_isDecodeFinished = true;
}

bool Decoder::readFrame() const
{
    int ret = av_read_frame(m_formatCtx, m_packet);
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            NEAPU_LOGI("End of file reached");
        } else {
            NEAPU_LOGE("Failed to read frame, error code {}", ret);
        }
        return false;
    }
    return true;
}

bool Decoder::sendPacket() const
{
    // 仅处理音频流的包
    if (!m_packet || m_packet->stream_index != m_audioStreamIndex) {
        return false;
    }

    const auto ret = avcodec_send_packet(m_codecCtx, m_packet);
    if (ret < 0) {
        char errMsg[256] = {0};
        av_strerror(ret, errMsg, sizeof(errMsg));
        NEAPU_LOGE("Failed to send packet to decoder, error code {}: {}", ret, errMsg);
        return false;
    }
    return true;
}

bool Decoder::recvFrame() const
{
    int ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false;
    }
    if (ret < 0) {
        char errMsg[256] = {0};
        av_strerror(ret, errMsg, sizeof(errMsg));
        NEAPU_LOGE("Failed to receive frame from decoder, error code {}: {}", ret, errMsg);
        return false;
    }
    return true;
}

void Decoder::processFrame()
{
    if (m_swrCtx) {
        // 需要重采样
        const int delay = static_cast<int>(swr_get_delay(m_swrCtx, m_frame->sample_rate));
        const int64_t dstNbSamples = av_rescale_rnd(
            delay + m_frame->nb_samples,
            m_mediaSampleRate,
            m_frame->sample_rate,
            AV_ROUND_UP);
        if (dstNbSamples > m_dstNbSamples) {
            av_freep(&m_dstData[0]);
            int ret = av_samples_alloc(
                &m_dstData[0],
                &m_dstLineSize,
                m_mediaChannels,
                static_cast<int>(dstNbSamples),
                static_cast<AVSampleFormat>(m_targetSampleFormat),
                1);
            if (ret < 0) {
                NEAPU_LOGE("Failed to allocate destination samples, error code {}", ret);
                return;
            }
            m_dstNbSamples = static_cast<int>(dstNbSamples);
        }

        int outSize = swr_convert(
            m_swrCtx,
            m_dstData,
            static_cast<int>(dstNbSamples),
            const_cast<const uint8_t**>(m_frame->data),
            m_frame->nb_samples);
        if (outSize < 0) {
            char errMsg[256];
            av_strerror(outSize, errMsg, sizeof(errMsg));
            NEAPU_LOGE("Failed to convert audio, error code {}: {}", outSize, errMsg);
            return;
        }

        auto pcmData = std::make_unique<PcmData>();
        pcmData->size = outSize * m_mediaChannels * av_get_bytes_per_sample(static_cast<AVSampleFormat>(m_targetSampleFormat));
        pcmData->data = new uint8_t[pcmData->size];
        std::memcpy(pcmData->data, m_dstData[0], pcmData->size);
        pcmData->nbSamples = outSize;

        enqueuePcmData(std::move(pcmData));

    } else {
        // 无需重采样，直接拷贝
        auto pcmData = std::make_unique<PcmData>();
        pcmData->size = m_frame->nb_samples * m_mediaChannels * av_get_bytes_per_sample(static_cast<AVSampleFormat>(m_frame->format));
        pcmData->data = new uint8_t[pcmData->size];
        std::memcpy(pcmData->data, m_frame->data[0], pcmData->size);
        pcmData->nbSamples = m_frame->nb_samples;

        enqueuePcmData(std::move(pcmData));
    }
}

void Decoder::enqueuePcmData(PcmDataPtr&& pcmData)
{
    std::unique_lock lock(m_queueMutex);
    while (m_waterLevel > HIGH_WATER_LEVEL && m_threadsRunning) {
        m_queueCondVar.wait(lock);
    }
    if (!m_threadsRunning) {
        return;
    }
    m_waterLevel += dataDuration(pcmData, m_mediaSampleRate, m_mediaChannels);
    m_pcmQueue.push(std::move(pcmData));
}

bool Decoder::initSwrContext()
{
    if (m_swrCtx) {
        return true;
    }

    if (!m_formatCtx || m_audioStreamIndex < 0) {
        NEAPU_LOGE("Format context or audio stream index invalid");
        return false;
    }

    auto* stream = m_formatCtx->streams[m_audioStreamIndex];
    AVSampleFormat inSampleFmt = m_codecCtx->sample_fmt;
    if (inSampleFmt == m_targetSampleFormat) {
        return true;
    }

    int inSampleRate = m_codecCtx->sample_rate;
    AVChannelLayout inLayout;
    av_channel_layout_copy(&inLayout, &m_codecCtx->ch_layout);
    if (inLayout.nb_channels == 0) {
        av_channel_layout_default(&inLayout, m_codecCtx->ch_layout.nb_channels);
    }
    if (inLayout.nb_channels == 0) {
        NEAPU_LOGE("Input frame has zero channels");
        return false;
    }

    int outSampleRate = inSampleRate;
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, inLayout.nb_channels);
    m_swrCtx = swr_alloc();
    int ret = swr_alloc_set_opts2(
        &m_swrCtx,
        &outLayout, static_cast<AVSampleFormat>(m_targetSampleFormat), outSampleRate,
        &inLayout, inSampleFmt, inSampleRate,
        0, nullptr
    );
    if (ret < 0) {
        NEAPU_LOGE("Failed to allocate SwrContext, error code {}", ret);
        swr_free(&m_swrCtx);
        return false;
    }

    // 初始化重采样上下文
    ret = swr_init(m_swrCtx);
    if (ret < 0) {
        NEAPU_LOGE("Failed to init SwrContext, error code {}", ret);
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
        return false;
    }

    m_dstNbSamples = 1024;
    ret = av_samples_alloc_array_and_samples(
        &m_dstData,
        &m_dstLineSize,
        outLayout.nb_channels,
        m_dstNbSamples,
        static_cast<AVSampleFormat>(m_targetSampleFormat),
        0);
    if (ret < 0) {
        NEAPU_LOGE("Failed to allocate destination samples, error code {}", ret);
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
        return false;
    }
    return true;
}