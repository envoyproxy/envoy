#pragma once

#include <string>

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "absl/cleanup/cleanup.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
#include <linux/filter.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test/mocks/network/mocks.h"
#endif

namespace Envoy {
namespace Quic {
namespace Extensions {
namespace ConnectionIdGenerator {
namespace Matcher {
namespace {

#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
// Runs `prog` against `data` via a socketpair + SO_ATTACH_FILTER and returns the program's return
// value.
//
// We can't attach via SO_ATTACH_REUSEPORT_CBPF, that would require a reuseport group sized to
// `concurrency`. SO_ATTACH_FILTER reinterprets the same uint32_t return as "bytes to deliver", so
// we read it back via recv(MSG_TRUNC) on a 1-byte buffer. EAGAIN/EWOULDBLOCK signals index 0, which
// SO_ATTACH_FILTER reinterprets as drop.
absl::StatusOr<uint32_t> runCbpf(const sock_fprog& prog, absl::string_view data, uint32_t min_len) {
  std::string padded(data);
  if (padded.size() < min_len) {
    padded.resize(min_len, '\0');
  }

  int sv[2];
  if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
    return absl::ErrnoToStatus(errno, "socketpair");
  }
  auto cleanup = absl::MakeCleanup([sv]() {
    ::close(sv[0]);
    ::close(sv[1]);
  });

  if (::setsockopt(sv[0], SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog)) < 0) {
    return absl::ErrnoToStatus(errno, "setsockopt(SO_ATTACH_FILTER)");
  }

  if (::send(sv[1], padded.data(), padded.size(), 0) < 0) {
    return absl::ErrnoToStatus(errno, "send");
  }

  char buf[1];
  ssize_t n = ::recv(sv[0], buf, sizeof(buf), MSG_TRUNC | MSG_DONTWAIT);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0u;
    } else {
      return absl::ErrnoToStatus(errno, "recv");
    }
  }

  return n;
}
#endif

} // namespace

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
      : concurrency_(concurrency),
        worker_selector_(factory.getCompatibleConnectionIdWorkerSelector(concurrency_)) {
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
    opt_ = factory.createCompatibleLinuxBpfSocketOption(concurrency);
    // Using a mock socket to capture the socket option which otherwise cannot be
    // extracted from the private field in the Socket::Option.
    Network::MockListenSocket mock_socket;
    EXPECT_CALL(mock_socket, setSocketOption(testing::_, testing::_, testing::_, testing::_))
        .WillOnce([this](int, int, const void* optval, socklen_t) {
          bpf_prog_ = static_cast<const sock_fprog*>(optval);
          return Api::SysCallIntResult{0, 0};
        });
    opt_->setOption(mock_socket, envoy::config::core::v3::SocketOption::STATE_BOUND);
#endif
  }
  uint32_t concurrency_;
  const QuicConnectionIdWorkerSelector worker_selector_;
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  Network::Socket::OptionConstSharedPtr opt_;
  const sock_fprog* bpf_prog_;
#endif
};

MATCHER_P2(FactoryFunctionsReturnWorkerId, packet, expected_id, "") {
  uint32_t id = arg.worker_selector_(*packet, 0xffffffff);
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  auto bpf_id_or = runCbpf(*arg.bpf_prog_, packet->toString(), arg.concurrency_);
  if (!bpf_id_or.ok()) {
    *result_listener << "\nrunCbpf failed: " << bpf_id_or.status();
    return false;
  }
  const uint32_t bpf_id = *bpf_id_or;

  // cBPF falls back to rxhash % concurrency, while bpf equivalent function returns 0xffffffff
  const bool bpf_ok =
      (expected_id == 0xffffffff) ? (bpf_id < arg.concurrency_) : (bpf_id == expected_id);
#else
  std::cout << "warning: not verifying bpf program due to lacking system support" << std::endl;
  const uint32_t bpf_id = expected_id;
  const bool bpf_ok = true;
#endif
  if (id != expected_id || !bpf_ok) {
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

} // namespace Matcher
} // namespace ConnectionIdGenerator
} // namespace Extensions
} // namespace Quic
} // namespace Envoy
