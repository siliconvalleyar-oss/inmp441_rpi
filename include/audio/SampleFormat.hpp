#pragma once

#include <cstdint>

namespace audio {

// Number of payload bits delivered by the INMP441 per channel slot.
inline constexpr int kSampleBits24 = 24;

// 2^23: full-scale positive value of a 24-bit two's-complement sample.
inline constexpr float kNormalize24 = 8388608.0f;

// The INMP441 delivers 24-bit two's-complement audio left-justified in the
// upper bits of each 32-bit I2S slot (this is fixed in hardware). The BCM2835
// PCM controller stores the slot verbatim, so the sample is recovered with an
// arithmetic right-shift of 8 bits.
inline int32_t rawToSample24(uint32_t raw) {
    return static_cast<int32_t>(raw) >> 8;
}

// Narrow a 24-bit sample to 16-bit (arithmetic shift preserves the sign).
inline int16_t sample24ToSample16(int32_t sample24) {
    return static_cast<int16_t>(sample24 >> 8);
}

// Convenience: raw 32-bit slot to a 16-bit sample in one step.
inline int16_t rawToSample16(uint32_t raw) {
    return static_cast<int16_t>(static_cast<int32_t>(raw) >> 16);
}

// Map a 24-bit sample to the [-1.0, 1.0) range.
inline float sample24ToFloat(int32_t sample24) {
    return static_cast<float>(sample24) / kNormalize24;
}

inline float rawToFloat(uint32_t raw) {
    return sample24ToFloat(rawToSample24(raw));
}

// One stereo frame, stored as 24-bit samples (int32 containers).
struct AudioFrame {
    int32_t left24 = 0;
    int32_t right24 = 0;

    int16_t left16() const { return sample24ToSample16(left24); }
    int16_t right16() const { return sample24ToSample16(right24); }
    float leftFloat() const { return sample24ToFloat(left24); }
    float rightFloat() const { return sample24ToFloat(right24); }
};

}  // namespace audio
