#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/string_view.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

std::unique_ptr<AltsChannelPool>
AltsChannelPool::create(absl::string_view handshaker_service_address) {
  // TODO(matthewstevenson88): Extend this to be configurable through API.
  std::size_t channel_pool_size = 10;
  std::vector<std::shared_ptr<grpc::Channel>> channel_pool;
  channel_pool.reserve(channel_pool_size);
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
  for (std::size_t i = 0; i < channel_pool_size; ++i) {
    channel_pool.push_back(grpc::CreateCustomChannel(
        std::string(handshaker_service_address), grpc::InsecureChannelCredentials(), channel_args));
  }
  return absl::WrapUnique(new AltsChannelPool(std::move(channel_pool)));
}

AltsChannelPool::AltsChannelPool(const std::vector<std::shared_ptr<grpc::Channel>>& channel_pool)
    : channel_pool_(channel_pool) {}

// TODO(matthewstevenson88): Add logic to limit number of outstanding channels.
std::shared_ptr<grpc::Channel> AltsChannelPool::getChannel() const {
  absl::BitGen gen;
  auto index = absl::Uniform<int>(gen, /*lo=*/0, channel_pool_.size());
  return channel_pool_[index];
}

std::size_t AltsChannelPool::getChannelPoolSize() const { return channel_pool_.size(); }

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
