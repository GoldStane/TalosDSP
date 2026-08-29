#include "audio/PassthroughCallback.h"

#include <cstring>

void PassthroughCallback::process(const float* input, float* output,
                                  std::uint32_t frames,
                                  std::uint32_t channels) noexcept {
  if (channels == 0 || frames == 0) {
    return;
  }
  if (input != nullptr && output != nullptr) {
    std::memcpy(output, input,
                static_cast<std::size_t>(frames) * channels * sizeof(float));
  } else if (output != nullptr) {
    // Input-only stream (e.g. loopback run with no mic): emit silence.
    std::memset(output, 0,
                static_cast<std::size_t>(frames) * channels * sizeof(float));
  }
}