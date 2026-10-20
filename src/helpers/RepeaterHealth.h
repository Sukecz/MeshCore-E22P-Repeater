#pragma once

#include <stdint.h>

namespace RepeaterHealth {

inline uint8_t receiveErrorPercent(uint32_t received, uint32_t errors) {
  const uint64_t attempts = (uint64_t)received + errors;
  return attempts == 0 ? 0 : (uint8_t)(((uint64_t)errors * 100) / attempts);
}

// Advisory thresholds for this build, not universal RF quality limits.
// Receive errors alone cannot establish a repeater fault.
inline const char* state(bool forwarding_disabled, uint32_t queued,
                         uint8_t receive_error_percent, uint8_t duplicate_percent) {
  if (forwarding_disabled || queued >= 16) return "bad";
  if (receive_error_percent >= 30 || duplicate_percent >= 50 || queued >= 8) return "warn";
  return "good";
}

}  // namespace RepeaterHealth
