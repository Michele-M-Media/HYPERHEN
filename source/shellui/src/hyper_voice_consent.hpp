#pragma once

#include <cstdint>

/* A local physical gesture is required independently of the worker command.
 * Its caller serializes access and supplies ShellUI's own monotonic clock. */
class HyperVoiceConsent {
 public:
  static constexpr uint32_t Combo = 0x8c00u;
  static constexpr uint32_t Cancel = 0x80002000u;

  void cancel() {
    released_ = holding_ = false;
    token_sequence_ = 0;
    token_user_ = -1;
  }

  bool observe(uint64_t sequence, uint64_t now, uint32_t buttons) {
    if (sequence == 0 || (have_sample_ && sequence <= last_sequence_)) return false;
    if (now == UINT64_MAX) {
      cancel();
      return false;
    }
    if (have_sample_ && (now < last_ms_ || now - last_ms_ >= 350u)) cancel();
    if (token_sequence_ &&
        (now < token_ms_ || now - token_ms_ >= 2000u)) {
      token_sequence_ = 0;
      token_user_ = -1;
    }
    last_sequence_ = sequence;
    last_ms_ = now;
    last_buttons_ = buttons;
    have_sample_ = true;
    if (buttons & Cancel) {
      cancel();
      return false;
    }
    if (buttons == 0) {
      released_ = true;
      holding_ = false;
      return false;
    }
    if (!released_ || buttons != Combo) {
      holding_ = false;
      return false;
    }
    if (!holding_) {
      holding_ = true;
      hold_ms_ = now;
      return false;
    }
    /* The worker independently requires 600 ms. The local 500 ms token is
     * ready before that command even when the two sampling loops differ. */
    if (now - hold_ms_ < 500u) return false;
    token_sequence_ = sequence;
    token_ms_ = now;
    token_user_ = -1;
    released_ = holding_ = false;
    return true;
  }

  void bind_user(int user) {
    if (user > 0 && user != 0xfe && user != 0xff && token_sequence_)
      token_user_ = user;
    else {
      token_user_ = -1;
      token_sequence_ = 0;
    }
  }

  bool consume(int user, uint64_t command_sequence, uint64_t now) {
    const bool valid = have_sample_ && token_sequence_ && user == token_user_ &&
        now != UINT64_MAX && now >= token_ms_ && now - token_ms_ < 2000u &&
        now >= last_ms_ && now - last_ms_ < 350u &&
        (last_buttons_ & Cancel) == 0 && command_sequence >= token_sequence_ &&
        command_sequence <= last_sequence_ &&
        last_sequence_ - command_sequence <= 128u;
    token_sequence_ = 0;
    token_user_ = -1;
    return valid;
  }

 private:
  uint64_t last_sequence_ = 0;
  uint64_t last_ms_ = 0;
  uint64_t hold_ms_ = 0;
  uint64_t token_sequence_ = 0;
  uint64_t token_ms_ = 0;
  uint32_t last_buttons_ = Cancel;
  int token_user_ = -1;
  bool have_sample_ = false;
  bool released_ = false;
  bool holding_ = false;
};
