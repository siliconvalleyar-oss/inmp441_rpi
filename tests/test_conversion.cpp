// Unit tests for the pure signal-conversion helpers (no hardware required).
// Run with: make test
#include <cmath>
#include <cstdio>

#include "audio/AudioProcessor.hpp"
#include "audio/SampleFormat.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__,   \
                         __LINE__, #cond);                       \
            ++failures;                                          \
        }                                                        \
    } while (0)

}  // namespace

int main() {
    using namespace audio;

    // Positive sample (24-bit value 0x7FFFFF left-justified in the slot).
    CHECK(rawToSample24(0x7FFFFF00U) == 0x7FFFFF);
    // Negative sample (sign extension propagates from bit 31).
    CHECK(rawToSample24(0x80000000U) == -0x800000);
    CHECK(rawToSample24(0xFFFFFF00U) == -1);
    // Low 8 bits are padding and must not affect the result.
    CHECK(rawToSample24(0x7FFFFF00U | 0xABU) == 0x7FFFFF);

    // 16-bit conversions.
    CHECK(sample24ToSample16(0x7FFFFF) == 0x7FFF);
    CHECK(sample24ToSample16(-0x800000) == -0x8000);
    CHECK(rawToSample16(0x7FFF0000U) == 0x7FFF);
    CHECK(rawToSample16(0x80000000U) == -0x8000);

    // Float mapping to [-1, 1).
    CHECK(sample24ToFloat(0x7FFFFF) > 0.9999f);
    CHECK(sample24ToFloat(0x7FFFFF) < 1.0f);
    CHECK(std::fabs(sample24ToFloat(0)) < 1e-6f);
    CHECK(sample24ToFloat(-0x800000) == -1.0f);
    CHECK(std::fabs(rawToFloat(0x40000000U) - 0.5f) < 1e-6f);

    // AudioFrame helpers.
    AudioFrame frame{0x7FFFFF, -0x800000};
    CHECK(frame.left16() == 0x7FFF);
    CHECK(frame.right16() == -0x8000);
    CHECK(frame.leftFloat() > 0.9999f);
    CHECK(frame.rightFloat() == -1.0f);

    // High-pass filter (header-only, no hardware needed).
    {
        // 0 Hz = disabled pass-through.
        HighPassFilter bypass;
        bypass.setCutoffHz(0.0, 48000);
        CHECK(!bypass.enabled());
        CHECK(bypass.process(12345) == 12345);
        CHECK(bypass.process(-100) == -100);

        // DC offset removal: a constant +0.5 FS input decays toward zero.
        HighPassFilter dc;
        dc.setCutoffHz(30.0, 48000);
        CHECK(dc.enabled());
        double last = 0.0;
        for (int i = 0; i < 48000; ++i) {  // 1 s at 48 kHz
            last = static_cast<double>(dc.process(16384)) / 32768.0;
        }
        CHECK(std::fabs(last) < 0.01);

        // Sub-bass is attenuated more than the mid band (cutoff 100 Hz): a
        // 20 Hz tone must come out clearly weaker than an 800 Hz one.
        auto settledRms = [](double freq, double cutoff, int samples) {
            HighPassFilter f;
            f.setCutoffHz(cutoff, 48000);
            double sum = 0.0;
            int counted = 0;
            for (int i = 0; i < samples; ++i) {
                const double phase = 2.0 * 3.14159265358979323846 * freq *
                                     static_cast<double>(i) / 48000.0;
                const int16_t s = static_cast<int16_t>(32767.0 * std::sin(phase));
                const double out = static_cast<double>(f.process(s)) / 32768.0;
                if (i >= samples / 2) {  // skip the settling transient
                    sum += out * out;
                    ++counted;
                }
            }
            return std::sqrt(sum / static_cast<double>(counted));
        };
        const double rms20 = settledRms(20.0, 100.0, 48000);
        const double rms800 = settledRms(800.0, 100.0, 48000);
        CHECK(rms20 < rms800 * 0.5);
    }

    if (failures == 0) {
        std::printf("test_conversion: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_conversion: %d check(s) FAILED\n", failures);
    return 1;
}
