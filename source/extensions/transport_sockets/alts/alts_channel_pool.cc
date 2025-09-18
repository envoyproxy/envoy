#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// TODO(matthewstevenson88): Extend this to be configurable through API.
constexpr std::size_t ChannelPoolSize = 10;
constexpr char UseGrpcExperimentalAltsHandshakerKeepaliveParams[] =
    "GRPC_EXPERIMENTAL_ALTS_HANDSHAKER_KEEPALIVE_PARAMS";

// 10 seconds
constexpr int ExperimentalKeepAliveTimeoutMs = 10 * 1000;
// 10 minutes
constexpr int ExperimentalKeepAliveTimeMs = 10 * 60 * 1000;

std::unique_ptr<AltsChannelPool>
AltsChannelPool::create(absl::string_view handshaker_service_address) {
  std::vector<std::shared_ptr<grpc::Channel>> channel_pool;
  channel_pool.reserve(ChannelPoolSize);
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
  const char* keep_alive = std::getenv(UseGrpcExperimentalAltsHandshakerKeepaliveParams);
  if (keep_alive != nullptr && std::strcmp(keep_alive, "true") == 0) {
    channel_args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, ExperimentalKeepAliveTimeoutMs);
    channel_args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, ExperimentalKeepAliveTimeMs);
  }
  for (std::size_t i = 0; i < ChannelPoolSize; ++i) {
    channel_pool.push_back(grpc::CreateCustomChannel(
        std::string(handshaker_service_address), grpc::InsecureChannelCredentials(), channel_args));
  }
  return absl::WrapUnique(new AltsChannelPool(std::move(channel_pool)));
}

AltsChannelPool::AltsChannelPool(const std::vector<std::shared_ptr<grpc::Channel>>& channel_pool)
    : channel_pool_(channel_pool) {}

// TODO(matthewstevenson88): Add logic to limit number of outstanding channels.
std::shared_ptr<grpc::Channel> AltsChannelPool::getChannel() {
  std::shared_ptr<grpc::Channel> channel;
  {
    absl::MutexLock lock(mu_);
    channel = channel_pool_[index_];
    index_ = (index_ + 1) % channel_pool_.size();
  }
  return channel;
}

std::size_t AltsChannelPool::getChannelPoolSize() const { return channel_pool_.size(); }

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
