//
// Created by liu86 on 2025/10/29.
//

#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "pub.h"

typedef struct AVFormatContext AVFormatContext;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVPacket AVPacket;
typedef struct AVFrame AVFrame;
typedef struct SwrContext SwrContext;

class Decoder {
public:
    bool open(const std::string& url);
    void close();

    int mediaSampleRate() const;
    int mediaChannels() const;

    bool isDecodeFinished() const { return m_isDecodeFinished.load(); }

    PcmDataPtr pullPcmData();

private:
    void decodeThreadFunc();
    bool readFrame() const;
    bool sendPacket() const;
    // 第一个bool为是否继续读取，第二个bool为是否读取成功
    bool recvFrame() const;
    void processFrame();
    void enqueuePcmData(PcmDataPtr&& pcmData);

    bool initSwrContext();

private:
    AVFormatContext* m_formatCtx{nullptr};
    AVCodecContext* m_codecCtx{nullptr};
    AVPacket* m_packet{nullptr};
    AVFrame* m_frame{nullptr};
    SwrContext* m_swrCtx{nullptr};
    int m_audioStreamIndex{-1};
    int m_mediaSampleRate{0};
    int m_mediaChannels{0};

    std::queue<PcmDataPtr> m_pcmQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondVar;
    double m_waterLevel{0};
    static constexpr double HIGH_WATER_LEVEL = 1.0; // 队列缓存1秒

    std::atomic_bool m_threadsRunning{false};
    std::thread m_decodeThread;

    uint8_t** m_dstData{nullptr};
    int m_dstLineSize{0};
    int m_dstNbSamples{0};

    std::atomic_bool m_isDecodeFinished{false};

    static constexpr int m_targetSampleFormat{1}; // AV_SAMPLE_FMT_S16
};
