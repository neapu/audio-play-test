#include <iostream>
#include <string>
#include "logger/logger.h"
#include "Decoder.h"
#include "AudioRenderer.h"

int main(int argc, char* argv[])
{
    neapu::Logger::setPrintLevel(NEAPU_LOG_LEVEL_INFO);
#ifdef DEBUG
    neapu::Logger::setLogLevel(NEAPU_LOG_LEVEL_DEBUG, SOURCE_DIR "/logs", "NeapuVideoPlayer");
#else
    neapu::Logger::setLogLevel(NEAPU_LOG_LEVEL_DEBUG, "logs", "NeapuVideoPlayer");
#endif
    if (argc < 2) {
        NEAPU_LOGE("缺少音频文件路径参数。用法: QtAudioTest <audio_file_path>");
        std::cerr << "用法: QtAudioTest <audio_file_path>" << std::endl;
        return -1;
    }
    std::string mediaPath = argv[1];
    Decoder decoder;
    if (!decoder.open(mediaPath)) {
        NEAPU_LOGE("Failed to open media file: {}", mediaPath);
        return -1;
    }

    AudioRenderer audioRenderer([&decoder]() -> PcmDataPtr {
        return decoder.pullPcmData();
    });

    if (!audioRenderer.start(decoder.mediaSampleRate(), decoder.mediaChannels())) {
        NEAPU_LOGE("Failed to start audio renderer");
        return -1;
    }

    while (true) {
        if (decoder.isDecodeFinished()) {
            if (!audioRenderer.isPlaying()) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    audioRenderer.stop();
    decoder.close();

    return 0;
}
