// Unit tests for the pure signal-conversion helpers (no hardware required).
// Run with: make test
#include <cmath>
#include <cstdio>

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

    if (failures == 0) {
        std::printf("test_conversion: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_conversion: %d check(s) FAILED\n", failures);
    return 1;
}
