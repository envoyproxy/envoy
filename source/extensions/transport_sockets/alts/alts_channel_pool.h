#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/channel.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// Manages a pool of gRPC channels to the ALTS handshaker service.
class AltsChannelPool {
public:
  static std::unique_ptr<AltsChannelPool> create(absl::string_view handshaker_service_address);

  // Gets a channel to the ALTS handshaker service. The caller is responsible
  // for checking that the channel is non-null.
  std::shared_ptr<grpc::Channel> getChannel();

  std::size_t getChannelPoolSize() const;

private:
  explicit AltsChannelPool(const std::vector<std::shared_ptr<grpc::Channel>>& channel_pool);

  // Use round-robin to select channels from the pool.
  absl::Mutex mu_;
  std::size_t index_ ABSL_GUARDED_BY(mu_) = 0;
  std::vector<std::shared_ptr<grpc::Channel>> channel_pool_;
};

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
