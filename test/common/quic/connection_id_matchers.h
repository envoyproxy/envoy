#pragma once

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace Matcher {

// A matcher for connection id factories to test both the bpf filter and worker selector
// functions. Recommended usage is like:
// EXPECT_THAT(FactoryFunctions(factory, concurrency), GivenPacket(buffer).ReturnsWorkerId(7))
//
// The matcher always validates the worker selector function, and, if bpf is available,
// also validates that the bpf program returns the same value.
//
// Wrapping in FactoryFunctions is necessary because calling the factory functions directly is
// not const.
class FactoryFunctions {
public:
  FactoryFunctions(EnvoyQuicConnectionIdGeneratorFactory& factory, uint32_t concurrency);

  const uint32_t concurrency_;
  const QuicConnectionIdWorkerSelector worker_selector_;
  Network::Socket::OptionConstSharedPtr opt_;
  const void* bpf_prog_ = nullptr;
};

class GivenPacket {
public:
  GivenPacket(const Buffer::Instance& packet);
  testing::Matcher<const FactoryFunctions&> ReturnsWorkerId(uint32_t id);
  testing::Matcher<const FactoryFunctions&> ReturnsDefaultWorkerId();

private:
  const Buffer::Instance& packet_;
};

} // namespace Matcher
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
