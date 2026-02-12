#include "AacEncoder.h"
#include <cstring>

AacEncoder::AacEncoder()
    : m_handle(nullptr),
      m_channels(0),
      m_initialized(false)
{
}

AacEncoder::~AacEncoder()
{
    close();
}

bool AacEncoder::init(int sampleRate,
                      int channels,
                      int bitrate,
                      TransmuxType type)
{
    if (aacEncOpen(&m_handle, 0, channels) != AACENC_OK)
        return false;

    m_channels = channels;

    aacEncoder_SetParam(m_handle, AACENC_AOT, AOT_AAC_LC);
    aacEncoder_SetParam(m_handle, AACENC_SAMPLERATE, sampleRate);
    aacEncoder_SetParam(m_handle, AACENC_CHANNELMODE,
                        channels == 1 ? MODE_1 : MODE_2);
    aacEncoder_SetParam(m_handle, AACENC_BITRATE, bitrate);
    aacEncoder_SetParam(m_handle, AACENC_TRANSMUX,
                        type == ADTS ? TT_MP4_ADTS : TT_MP4_RAW);
    aacEncoder_SetParam(m_handle, AACENC_AFTERBURNER, 1);

    if (aacEncEncode(m_handle, NULL, NULL, NULL, NULL) != AACENC_OK)
        return false;

    m_initialized = true;
    return true;
}

bool AacEncoder::encode(const int16_t* pcm,
                        int samples,
                        std::vector<uint8_t>& outData)
{
    if (!m_initialized)
        return false;

    AACENC_BufDesc inBuf = {0}, outBuf = {0};
    AACENC_InArgs inArgs = {0};
    AACENC_OutArgs outArgs = {0};

    void* inPtr = (void*)pcm;
    int inIdentifier = IN_AUDIO_DATA;
    int inSize = samples * sizeof(int16_t);
    int inElemSize = sizeof(int16_t);

    inArgs.numInSamples = samples;

    inBuf.numBufs = 1;
    inBuf.bufs = &inPtr;
    inBuf.bufferIdentifiers = &inIdentifier;
    inBuf.bufSizes = &inSize;
    inBuf.bufElSizes = &inElemSize;

    uint8_t outBuffer[8192];
    void* outPtr = outBuffer;
    int outIdentifier = OUT_BITSTREAM_DATA;
    int outSize = sizeof(outBuffer);
    int outElemSize = sizeof(uint8_t);

    outBuf.numBufs = 1;
    outBuf.bufs = &outPtr;
    outBuf.bufferIdentifiers = &outIdentifier;
    outBuf.bufSizes = &outSize;
    outBuf.bufElSizes = &outElemSize;

    if (aacEncEncode(m_handle, &inBuf, &outBuf, &inArgs, &outArgs) != AACENC_OK)
        return false;

    if (outArgs.numOutBytes > 0) {
        outData.insert(outData.end(),
                       outBuffer,
                       outBuffer + outArgs.numOutBytes);
    }

    return true;
}

bool AacEncoder::flush(std::vector<uint8_t>& outData)
{
    if (!m_initialized)
        return false;

    while (true) {
        AACENC_BufDesc outBuf = {0};
        AACENC_InArgs inArgs = {0};
        AACENC_OutArgs outArgs = {0};

        uint8_t outBuffer[8192];
        void* outPtr = outBuffer;
        int outIdentifier = OUT_BITSTREAM_DATA;
        int outSize = sizeof(outBuffer);
        int outElemSize = sizeof(uint8_t);

        outBuf.numBufs = 1;
        outBuf.bufs = &outPtr;
        outBuf.bufferIdentifiers = &outIdentifier;
        outBuf.bufSizes = &outSize;
        outBuf.bufElSizes = &outElemSize;

        if (aacEncEncode(m_handle, NULL, &outBuf, &inArgs, &outArgs) != AACENC_OK)
            return false;

        if (outArgs.numOutBytes == 0)
            break;

        outData.insert(outData.end(),
                       outBuffer,
                       outBuffer + outArgs.numOutBytes);
    }

    return true;
}

void AacEncoder::close()
{
    if (m_handle) {
        aacEncClose(&m_handle);
        m_handle = nullptr;
    }
    m_initialized = false;
}
