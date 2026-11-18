// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_AUDIO_BUFFER_HPP
#define LINA_ENGINE_AUDIO_BUFFER_HPP

#include <cstddef>
#include <vector>

namespace lina::engine {

/// Audio that the engine has produced: one vector of samples per channel, all
/// of the same length, at one sample rate.
struct AudioBuffer {
  double sampleRate = 0.0;
  std::vector<std::vector<float>> channels;

  /// Number of sample frames, which is the length of every channel.
  [[nodiscard]] std::size_t frameCount() const {
    return channels.empty() ? 0 : channels.front().size();
  }
};

} // namespace lina::engine

#endif // LINA_ENGINE_AUDIO_BUFFER_HPP
