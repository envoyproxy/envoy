#pragma once

#include <string>

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
#include <linux/bpf.h>
#include <linux/filter.h>
#include "test/mocks/network/mocks.h"
#endif

namespace Envoy {
namespace Quic {
namespace Matcher {

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__) &&                                     \
    defined(TODO_TEST_FOR_BPF_PROG_RUN_SUPPORT)
#define SUPPORTS_TESTING_BPF_PROG
#endif

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
  FactoryFunctions(EnvoyQuicConnectionIdGeneratorFactory& factory, uint32_t concurrency)
      : worker_selector_(factory.getCompatibleConnectionIdWorkerSelector(concurrency)) {
#ifdef SUPPORTS_TESTING_BPF_PROG
    Network::Socket::OptionConstSharedPtr sockopt =
        factory.createCompatibleLinuxBpfSocketOption(concurrency);
    // Using a mock socket to capture the socket option which otherwise cannot be
    // extracted from the private field in the Socket::Option.
    Network::MockListenSocket mock_socket;
    const sock_fprog* bpf_prog;
    EXPECT_CALL(mock_socket, setSocketOption(testing::_, testing::_, testing::_, testing::_))
        .WillOnce([this](int, int, const void* optval, socklen_t) {
          bpf_prog_ = static_cast<const sock_fprog*>(optval);
          return Api::SysCallIntResult{0, 0};
        });
    sockopt->setOption(mock_socket, envoy::config::core::v3::SocketOption::STATE_BOUND);
#endif
  }
  const QuicConnectionIdWorkerSelector worker_selector_;
#ifdef SUPPORTS_TESTING_BPF_PROG
  const sock_fprog* bpf_prog_;
#endif
};

MATCHER_P2(FactoryFunctionsReturnWorkerId, packet, expected_id, "") {
  uint32_t id = arg.worker_selector_(*packet, 0xffffffff);
  // TODO: run the bpf program, something like the below. The headers we have in the docker
  //       container don't seem to provide `bpf()` or `bpf_prog_run()`.
#ifdef SUPPORTS_TESTING_BPF_PROG
  std::string bytes = packet->toString();
  uint32_t bpf_id = bpf_prog_run(arg.bpf_prog_, bytes.c_str());
#else
  uint32_t bpf_id = expected_id;
  std::cout << "warning: not verifying bpf program due to lacking system support" << std::endl;
#endif
  if (id != expected_id || bpf_id != expected_id) {
    *result_listener << "\nworker selector function returned " << id;
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
    *result_listener << "\nbpf function returned " << bpf_id;
#endif
    return false;
  }
  return true;
}

class GivenPacket {
public:
  GivenPacket(const Buffer::Instance& packet) : packet_(packet) {}
  testing::Matcher<const FactoryFunctions&> ReturnsWorkerId(uint32_t id) {
    return FactoryFunctionsReturnWorkerId(&packet_, id);
  }
  testing::Matcher<const FactoryFunctions&> ReturnsDefaultWorkerId() {
    return FactoryFunctionsReturnWorkerId(&packet_, 0xffffffff);
  }

private:
  const Buffer::Instance& packet_;
};

#undef SUPPORTS_TESTING_BPF_PROG

} // namespace Matcher
} // namespace Quic
} // namespace Envoy
