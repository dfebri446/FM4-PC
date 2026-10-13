#pragma once
#include <rex/input/input_driver.h>
#include <rex/ui/virtual_key.h>
#include "fm4-diagnostic-state.h"
#include <chrono>
#include <fstream>
#include <mutex>
#include <cstdio>

// Opt-in, application-local controller for reproducible startup tests. It never
// sends desktop input. Missing or malformed files mean all controls released.
class FM4TestInput final : public rex::input::InputDriver {
 public:
  using X_STATUS = rex::X_STATUS;
  using X_RESULT = rex::X_RESULT;
  explicit FM4TestInput(std::string path)
      : InputDriver(nullptr, 0), path_(std::move(path)) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "FM4 diagnostic controller";
    info.synthetic = true;
    out.push_back(info);
  }
  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_read_) {
      next_read_ = now + std::chrono::milliseconds(50);
      int buttons = 0, lt = 0, rt = 0, lx = 0, ly = 0, rx = 0, ry = 0;
      std::ifstream file(path_);
      rex::input::X_INPUT_STATE next{};
      if ((file >> buttons >> lt >> rt >> lx >> ly >> rx >> ry) &&
          buttons >= 0 && buttons <= 65535 && lt >= 0 && lt <= 255 && rt >= 0 && rt <= 255 &&
          lx >= -32768 && lx <= 32767 && ly >= -32768 && ly <= 32767 &&
          rx >= -32768 && rx <= 32767 && ry >= -32768 && ry <= 32767) {
        next.gamepad.buttons = uint16_t(buttons);
        next.gamepad.left_trigger = uint8_t(lt);
        next.gamepad.right_trigger = uint8_t(rt);
        next.gamepad.thumb_lx = int16_t(lx);
        next.gamepad.thumb_ly = int16_t(ly);
        next.gamepad.thumb_rx = int16_t(rx);
        next.gamepad.thumb_ry = int16_t(ry);
      }
      next.packet_number = ++packet_;
      if (uint16_t(next.gamepad.buttons) != uint16_t(state_.gamepad.buttons))
        std::fprintf(stderr, "FM4 test input: buttons=%u packet=%u\n", uint16_t(next.gamepad.buttons), packet_);
      state_ = next;
      fm4_diagnostic_buttons.store(uint16_t(state_.gamepad.buttons), std::memory_order_relaxed);
    }
    if (out) *out = state_;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) {
      *out = {};
      out->type = 1;
      out->sub_type = 1;
      // Match the standard SDL gamepad's supported-bit masks. Capabilities
      // describe available bits, not the largest positive analog value.
      out->gamepad.buttons = uint16_t(0xF3FF);
      out->gamepad.left_trigger = out->gamepad.right_trigger = 255;
      out->gamepad.thumb_lx = out->gamepad.thumb_ly = int16_t(-1);
      out->gamepad.thumb_rx = out->gamepad.thumb_ry = int16_t(-1);
      out->vibration.left_motor_speed = out->vibration.right_motor_speed = uint16_t(0xFFFF);
      // A local diagnostic switch allows comparison without rebuilding.
      int legacy = 0;
      std::ifstream(path_ + ".legacy-caps") >> legacy;
      if (legacy) {
        out->gamepad.buttons = uint16_t(0xFFFF);
        out->gamepad.thumb_lx = out->gamepad.thumb_ly = int16_t(32767);
        out->gamepad.thumb_rx = out->gamepad.thumb_ry = int16_t(32767);
        out->vibration.left_motor_speed = out->vibration.right_motor_speed = 0;
      }
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                             rex::input::X_INPUT_KEYSTROKE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out) return X_ERROR_BAD_ARGUMENTS;
    GetDeviceState(id, nullptr);
    std::lock_guard lock(mutex_);
    using Key = rex::ui::VirtualKey;
    static constexpr Key keys[] = {
      Key::kXInputPadDpadUp, Key::kXInputPadDpadDown, Key::kXInputPadDpadLeft,
      Key::kXInputPadDpadRight, Key::kXInputPadStart, Key::kXInputPadBack,
      Key::kXInputPadLThumbPress, Key::kXInputPadRThumbPress,
      Key::kXInputPadLShoulder, Key::kXInputPadRShoulder, Key::kNone, Key::kNone,
      Key::kXInputPadA, Key::kXInputPadB, Key::kXInputPadX, Key::kXInputPadY};
    if (++keystroke_queries_ <= 3)
      std::fprintf(stderr, "FM4 diagnostic keystroke query n=%u\n", keystroke_queries_);
    const uint16_t current = state_.gamepad.buttons;
    for (unsigned bit = 0; bit < 16; ++bit) {
      const uint16_t mask = uint16_t(1u << bit);
      if (!((current ^ keystroke_buttons_) & mask)) continue;
      keystroke_buttons_ ^= mask;
      if (keys[bit] == Key::kNone) continue;
      *out = {};
      out->virtual_key = uint16_t(keys[bit]);
      out->flags = current & mask ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
      std::fprintf(stderr, "FM4 diagnostic keystroke key=%x flags=%x\n", uint16_t(out->virtual_key), uint16_t(out->flags));
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_EMPTY;
  }
 private:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x464D3400);
  std::string path_;
  std::mutex mutex_;
  std::chrono::steady_clock::time_point next_read_{};
  uint32_t packet_ = 0;
  uint16_t keystroke_buttons_ = 0;
  unsigned keystroke_queries_ = 0;
  rex::input::X_INPUT_STATE state_{};
};
