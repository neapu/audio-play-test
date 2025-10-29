#pragma once
#include <cstdint>
#include <memory>

struct PcmData {
    ~PcmData();
    uint8_t* data{nullptr};
    size_t size{0};
    int nbSamples{0};
};

using PcmDataPtr = std::unique_ptr<PcmData>;