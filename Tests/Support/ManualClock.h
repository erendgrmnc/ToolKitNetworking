#pragma once

#include <cstdint>

namespace ToolKit::ToolKitNetworking {
class ManualClock {
public:
  uint64_t NowMs() const { return m_nowMs; }
  void AdvanceMs(uint64_t deltaMs) { m_nowMs += deltaMs; }
  void SetNowMs(uint64_t nowMs) { m_nowMs = nowMs; }

private:
  uint64_t m_nowMs = 0;
};
} // namespace ToolKit::ToolKitNetworking
