// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/testing/test_plugins.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

namespace lina::engine::testing {

namespace {

namespace te = tracktion;

constexpr const char *delayFramesProperty = "delayFrames";
constexpr const char *gainProperty = "gain";

/// The frame inside a block that a MIDI message belongs to. Its timestamp is
/// in seconds from the start of the block; the engine's own plugin hosting
/// rounds the same way.
int frameOf(const juce::MidiMessage &message, double sampleRate, int frameCount)
    [[clang::nonblocking]] {
  const auto frame =
      static_cast<int>(std::lround(message.getTimeStamp() * sampleRate));
  return std::clamp(frame, 0, frameCount - 1);
}

} // namespace

ToneInstrument::ToneInstrument(const te::PluginCreationInfo &info)
    : Plugin(info) {}

ToneInstrument::~ToneInstrument() { notifyListenersOfDeletion(); }

juce::ValueTree ToneInstrument::create() {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName);
}

void ToneInstrument::initialise(const te::PluginInitialisationInfo &info) {
  sampleRate_ = info.sampleRate;
  silence();
}

void ToneInstrument::deinitialise() { silence(); }

void ToneInstrument::reset() { silence(); }

void ToneInstrument::silence() [[clang::nonblocking]] {
  for (auto &voice : voices_) {
    voice.active = false;
  }
}

void ToneInstrument::handle(const juce::MidiMessage &message)
    [[clang::nonblocking]] {
  if (message.isNoteOn()) {
    // With every voice busy the first one is replaced.
    Voice *target = &voices_.front();
    for (auto &voice : voices_) {
      if (!voice.active) {
        target = &voice;
        break;
      }
    }
    const int note = message.getNoteNumber();
    *target = {
        .active = true,
        .note = note,
        .level = static_cast<double>(message.getVelocity()) / 127.0,
        .frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0),
        .age = 0,
    };
  } else if (message.isNoteOff()) {
    for (auto &voice : voices_) {
      if (voice.active && voice.note == message.getNoteNumber()) {
        voice.active = false;
      }
    }
  } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
    silence();
  }
}

void ToneInstrument::render(const te::PluginRenderContext &context, int from,
                            int to) [[clang::nonblocking]] {
  auto &buffer = *context.destBuffer;
  for (int frame = from; frame < to; ++frame) {
    double sum = 0.0;
    for (auto &voice : voices_) {
      if (voice.active) {
        const double phase = 2.0 * std::numbers::pi * voice.frequency *
                             static_cast<double>(voice.age) / sampleRate_;
        sum += voice.level * std::cos(phase);
        ++voice.age;
      }
    }
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
      buffer.setSample(channel, context.bufferStartSample + frame,
                       static_cast<float>(sum));
    }
  }
}

void ToneInstrument::applyToBuffer(const te::PluginRenderContext &context)
    [[clang::nonblocking]] {
  if (context.destBuffer == nullptr) {
    return;
  }
  const int frameCount = context.bufferNumSamples;
  int done = 0;
  if (context.bufferForMidiMessages != nullptr) {
    if (context.bufferForMidiMessages->isAllNotesOff) {
      silence();
    }
    for (const auto &message : *context.bufferForMidiMessages) {
      if (frameCount > 0) {
        const int frame = frameOf(message, sampleRate_, frameCount);
        render(context, done, std::max(done, frame));
        done = std::max(done, frame);
      }
      handle(message);
    }
  }
  render(context, done, frameCount);
}

DelayGainEffect::DelayGainEffect(const te::PluginCreationInfo &info)
    : Plugin(info) {}

DelayGainEffect::~DelayGainEffect() { notifyListenersOfDeletion(); }

juce::ValueTree DelayGainEffect::create(int delayFrames, float gain) {
  return te::createValueTree(te::IDs::PLUGIN, te::IDs::type, xmlTypeName,
                             juce::Identifier(delayFramesProperty), delayFrames,
                             juce::Identifier(gainProperty), gain);
}

void DelayGainEffect::initialise(
    [[maybe_unused]] const te::PluginInitialisationInfo &info) {
  const int delayFrames =
      std::max(0, static_cast<int>(state.getProperty(
                      juce::Identifier(delayFramesProperty))));
  gain_ = static_cast<float>(state.getProperty(juce::Identifier(gainProperty)));
  for (auto &channel : history_) {
    channel.assign(static_cast<std::size_t>(delayFrames), 0.0F);
  }
  position_ = 0;
}

void DelayGainEffect::deinitialise() { reset(); }

void DelayGainEffect::reset() {
  for (auto &channel : history_) {
    std::ranges::fill(channel, 0.0F);
  }
  position_ = 0;
}

void DelayGainEffect::applyToBuffer(const te::PluginRenderContext &context)
    [[clang::nonblocking]] {
  if (context.destBuffer == nullptr) {
    return;
  }
  auto &buffer = *context.destBuffer;
  const auto channelCount =
      std::min(static_cast<std::size_t>(buffer.getNumChannels()), maxChannels);
  const auto frameCount = static_cast<std::size_t>(context.bufferNumSamples);
  std::size_t position = position_;
  for (std::size_t channel = 0; channel < channelCount; ++channel) {
    // at() checks the index and never fails here: `channel` is below
    // maxChannels and `position` below the length of the history.
    auto &history = history_.at(channel);
    const std::span samples(buffer.getWritePointer(static_cast<int>(channel),
                                                   context.bufferStartSample),
                            frameCount);
    position = position_;
    for (float &sample : samples) {
      float delayed = sample;
      if (!history.empty()) {
        delayed = history.at(position);
        history.at(position) = sample;
        position = (position + 1) % history.size();
      }
      sample = gain_ * delayed;
    }
  }
  position_ = position;
}

} // namespace lina::engine::testing
