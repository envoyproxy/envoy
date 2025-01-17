#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"

#include <algorithm>
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

std::unique_ptr<AltsChannelPool>
AltsChannelPool::create(absl::string_view handshaker_service_address) {
  std::vector<std::shared_ptr<grpc::Channel>> channel_pool;
  channel_pool.reserve(ChannelPoolSize);
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
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
    absl::MutexLock lock(&mu_);
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
