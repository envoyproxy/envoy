#include "network_utility.h"

namespace Network {
namespace Test {
namespace {
const int kFirstEphemeralPort = 40152;
const int kLastEphemeralPort = 65535;
const int kNumEphemeralPorts = kLastEphemeralPort - kFirstEphemeralPort + 1;
} // namespace

int getUnusedPort() {
  static int next_port = kFirstEphemeralPort;
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
