#ifndef AAC_ENCODER_H
#define AAC_ENCODER_H

#include <vector>
#include <cstdint>
#include <string>
#include "FDKaac.h"

class AacEncoder
{
public:
    enum TransmuxType {
        ADTS,
        RAW
    };

    AacEncoder();
    ~AacEncoder();

    bool init(int sampleRate,
              int channels,
              int bitrate = 128000,
              TransmuxType type = ADTS);

    bool encode(const int16_t* pcm,
                int samples,
                std::vector<uint8_t>& outData);

    bool flush(std::vector<uint8_t>& outData);

    void close();

private:
    HANDLE_AACENCODER m_handle;
    int m_channels;
    bool m_initialized;
};

#endif
