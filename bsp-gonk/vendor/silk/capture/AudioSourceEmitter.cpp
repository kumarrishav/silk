//#define LOG_NDEBUG 0
#define LOG_TAG "silk-capture"
#include <utils/Log.h>

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/AudioSource.h>

#include "AudioSourceEmitter.h"
#include "Channel.h"

// TODO: This hard code must match ext/clapper
#define FFT_WINDOW_SIZE 512    // 32ms - units of samples

// Batch this number of audio sample windows before sending off to node. Else,
// we'd be flooding node with data every 20ms, which incurs lots o overhead.
#define WINDOWS_PER_PACKET 3 // 96ms of data per TAG_MIC packet

// AudioSource is always 16 bit PCM (2 bytes / sample)
#define BYTES_PER_SAMPLE 2

using namespace android;

AudioSourceEmitter::AudioSourceEmitter(Channel* channel, const sp<MediaSource> &source) :
    mChannel(channel),
    mSource(source) {
  mAudioBufferIdx = 0;

  mAudioBufferLen = FFT_WINDOW_SIZE * BYTES_PER_SAMPLE *
      WINDOWS_PER_PACKET * sAudioChannels;
  mAudioBuffer = nullptr;
}

AudioSourceEmitter::~AudioSourceEmitter() {
  free(mAudioBuffer);
}

status_t AudioSourceEmitter::start(MetaData *params) {
  return mSource->start(params);
}

status_t AudioSourceEmitter::stop() {
  return mSource->stop();
}

sp<MetaData> AudioSourceEmitter::getFormat() {
  return mSource->getFormat();
}

status_t AudioSourceEmitter::read(MediaBuffer **buffer,
    const ReadOptions *options) {
  status_t err = mSource->read(buffer, options);

  if (err == ::OK && (*buffer) && (*buffer)->range_length()) {
    uint8_t *data = static_cast<uint8_t *>((*buffer)->data()) + (*buffer)->range_offset();
    uint32_t len = (*buffer)->range_length();

    // If these next samples will overrun the buffer, then send out data now
    if (mAudioBufferIdx + len > mAudioBufferLen) {
      uint32_t fillLen = mAudioBufferLen - mAudioBufferIdx;
      if (fillLen > 0) {
        // Top off the buffer to ensure that the packet is evenly divisible by
        // the fft window size (mAudioBufferLen)
        memcpy(mAudioBuffer + mAudioBufferIdx, data, fillLen);
        data += fillLen;
        len -= fillLen;
      }

      mChannel->send(Channel::TAG_MIC, mAudioBuffer, mAudioBufferLen, free, mAudioBuffer);
      mAudioBuffer = nullptr;
      mAudioBufferIdx = 0;
    }

    // let's assume we will never get a set samples larger than our full buffer
    CHECK(mAudioBufferIdx + len <= mAudioBufferLen);

    // batch samples
    if (mAudioBuffer == nullptr) {
      mAudioBuffer = (uint8_t *) malloc(mAudioBufferLen);
      CHECK(mAudioBuffer != nullptr);
    }
    memcpy(mAudioBuffer + mAudioBufferIdx, data, len);
    mAudioBufferIdx += len;
 }

  return err;
}
