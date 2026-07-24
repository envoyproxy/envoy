#pragma once

#include <cstdint>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/socket.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

// Attaches a classic BPF program to a SO_REUSEPORT group that steers each new
// connection to the worker socket pinned to the CPU that received it. `worker_cpus`
// holds the CPU pinned to each worker indexed by worker. The kernel uses the program
// result as the reuse port socket index, so the program returns the worker index on a
// CPU match and a balanced fallback otherwise. The program is built and installed
// during option application, so the kernel copies it within the synchronous setsockopt
// call.
class ReusePortBpfCpuSteeringOptionImpl : public Socket::Option,
                                          Logger::Loggable<Logger::Id::connection> {
public:
  explicit ReusePortBpfCpuSteeringOptionImpl(std::vector<uint32_t> worker_cpus)
      : worker_cpus_(std::move(worker_cpus)) {}

  // Socket::Option
  bool setOption(Socket& socket,
                 envoy::config::core::v3::SocketOption::SocketState state) const override;
  void hashKey(std::vector<uint8_t>& hash_key) const override;
  std::optional<Details>
  getOptionDetails(const Socket& socket,
                   envoy::config::core::v3::SocketOption::SocketState state) const override;
  bool isSupported() const override;

private:
  const std::vector<uint32_t> worker_cpus_;
};

} // namespace Network
} // namespace Envoy
