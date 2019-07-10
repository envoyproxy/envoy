#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wtype-limits"

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_simple_buffer_allocator.h"

#pragma GCC diagnostic pop

#include "extensions/quic_listeners/quiche/platform/envoy_quic_clock.h"

namespace Envoy  {
namespace Quic {

class EnvoyQuicConnectionHelper : public quic::QuicConnectionHelperInterface {
public:
  EnvoyQuicConnectionHelper(Event::TimeSystem& time_system)
      : clock_(time_system), random_generator_(quic::QuicRandom::GetInstance()) {}

  ~EnvoyQuicConnectionHelper() override {}

  // QuicConnectionHelperInterface
  const quic::QuicClock* GetClock() const override { return &clock_; }
  quic::QuicRandom* GetRandomGenerator() override { return random_generator_; }
  quic::QuicBufferAllocator* GetStreamSendBufferAllocator() override { return &buffer_allocator_; }

private:
  EnvoyQuicClock clock_;
  quic::QuicRandom* random_generator_ = nullptr;
  quic::SimpleBufferAllocator buffer_allocator_;
};

} // namespace Quic
} // namespace Envoy
