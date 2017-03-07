#include "network_utility.h"

#include "common/runtime/runtime_impl.h"

#include <mutex>

namespace Network {
namespace Test {
namespace {
const int kFirstEphemeralPort = 40152;
const int kLastEphemeralPort = 65535;

int selectFirstPort() {
  uint64_t range = kLastEphemeralPort - kFirstEphemeralPort + 1;
  Runtime::RandomGeneratorImpl rng;
  uint64_t rand_port = (rng.random() % range) + kFirstEphemeralPort;
  return static_cast<int>(rand_port);
}
} // namespace

int getUnusedPort() {
  static int next_port = selectFirstPort();
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  if (next_port > kLastEphemeralPort) {
    next_port = kFirstEphemeralPort;
  }
  // HACK ALERT: For now, we're just returning the next port, not checking if it is free.
  return next_port++;
}

} // Test
} // Network
