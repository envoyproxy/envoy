#include "source/extensions/transport_sockets/alts/config.h"

#include <memory>
#include <utility>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.validate.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/network/address.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/google_grpc_context.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/grpc_tsi.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "absl/container/node_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "grpcpp/channel.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

// Manage ALTS singleton state via SingletonManager
class AltsSharedState : public Singleton::Instance {
public:
  explicit AltsSharedState(absl::string_view handshaker_service_address)
      : channel_pool_(AltsChannelPool::create(handshaker_service_address)) {}

  ~AltsSharedState() override = default;

  std::shared_ptr<grpc::Channel> getChannel() const { return channel_pool_->getChannel(); }

private:
  // There is blanket google-grpc initialization in MainCommonBase, but that
  // doesn't cover unit tests. However, putting blanket coverage in ProcessWide
  // causes background threaded memory allocation in all unit tests making it
  // hard to measure memory. Thus we also initialize grpc using our idempotent
  // wrapper-class in classes that need it. See
  // https://github.com/envoyproxy/envoy/issues/8282 for details.
#ifdef ENVOY_GOOGLE_GRPC
  Grpc::GoogleGrpcContext google_grpc_context_;
#endif
  std::unique_ptr<AltsChannelPool> channel_pool_;
};

SINGLETON_MANAGER_REGISTRATION(alts_shared_state);

// Returns true if the peer's service account is found in peers, otherwise
// returns false and fills out err with an error message.
bool doValidate(const absl::node_hash_set<std::string>& peers, TsiInfo& tsi_info,
                std::string& err) {
  if (peers.contains(tsi_info.peer_identity_)) {
    return true;
  }
  err =
      "Couldn't find peer's service account in peer_service_accounts: " + absl::StrJoin(peers, ",");
  return false;
}

HandshakeValidator
createHandshakeValidator(const envoy::extensions::transport_sockets::alts::v3::Alts& config) {
  const auto& peer_service_accounts = config.peer_service_accounts();
  const absl::node_hash_set<std::string> peers(peer_service_accounts.cbegin(),
                                               peer_service_accounts.cend());
  HandshakeValidator validator;
  // Skip validation if peers is empty.
  if (!peers.empty()) {
    validator = [peers](TsiInfo& tsi_info, std::string& err) {
      return doValidate(peers, tsi_info, err);
    };
  }
  return validator;
}

// Returns the configuration of Envoy's async gRPC client to the ALTS handshaker
// service. Its channel has the args of the channels of AltsChannelPool. In
// particular, each worker's client gets its own connection, rather than sharing
// one with the other workers, so that workers don't share the handshaker
// service's per-connection limit on concurrent streams.
envoy::config::core::v3::GrpcService
createHandshakerGrpcService(absl::string_view handshaker_service_address) {
  envoy::config::core::v3::GrpcService grpc_service;
  auto* google_grpc = grpc_service.mutable_google_grpc();
  google_grpc->set_target_uri(handshaker_service_address);
  google_grpc->set_stat_prefix("alts_handshaker");
  auto& channel_args = *google_grpc->mutable_channel_args()->mutable_args();
  for (const auto& [key, value] : AltsChannelPool::channelArgs()) {
    channel_args[key].set_int_value(value);
  }
  return grpc_service;
}

template <class TransportSocketFactoryPtr>
TransportSocketFactoryPtr createTransportSocketFactoryHelper(
    const Protobuf::Message& message, bool is_upstream,
    Server::Configuration::TransportSocketFactoryContext& factory_ctxt) {
  auto config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::alts::v3::Alts&>(
          message, factory_ctxt.messageValidationVisitor());
  HandshakeValidator validator = createHandshakeValidator(config);
  const std::string& handshaker_service_address = config.handshaker_service();

  // A reference to this is held in the factory closure to keep the singleton
  // instance alive. Its channels serve the handshakers that make blocking calls
  // to the handshaker service.
  auto alts_shared_state =
      factory_ctxt.serverFactoryContext().singletonManager().getTyped<AltsSharedState>(
          SINGLETON_MANAGER_REGISTERED_NAME(alts_shared_state), [handshaker_service_address] {
            return std::make_shared<AltsSharedState>(handshaker_service_address);
          });

  // Downstream handshakers talk to the handshaker service through Envoy's async
  // gRPC client, so that a worker never waits for the handshaker service. Each
  // worker gets its client from the cluster manager's cache, where it is shared
  // by all the ALTS transport sockets that use the same handshaker service. A
  // worker then no longer limits how many of its handshakes wait for the
  // handshaker service at once; listener connection limits and the overload
  // manager still apply.
  //
  // Upstream handshakers still make blocking calls. Upstream connections live in
  // the cluster manager's thread-local connection pools, which a worker destroys
  // after the thread-local state of the async gRPC client, so an upstream
  // handshake in progress could outlive its stream.
  // TODO(NetanelPersikGoogle): make upstream handshakes asynchronous too.
  std::shared_ptr<const Grpc::GrpcServiceConfigWithHashKey> handshaker_grpc_service;
  Upstream::ClusterManager* cluster_manager = nullptr;
  Stats::Scope* stats_scope = nullptr;
  if (!is_upstream) {
    cluster_manager = &factory_ctxt.serverFactoryContext().clusterManager();
    stats_scope = &factory_ctxt.serverFactoryContext().serverScope();
    envoy::config::core::v3::GrpcService grpc_service =
        createHandshakerGrpcService(handshaker_service_address);
    // The async gRPC client needs Google gRPC. Check once here, rather than on
    // every handshake.
    if (cluster_manager->grpcAsyncClientManager()
            .factoryForGrpcService(grpc_service, *stats_scope, /*skip_cluster_check=*/true)
            .ok()) {
      handshaker_grpc_service =
          std::make_shared<const Grpc::GrpcServiceConfigWithHashKey>(grpc_service);
    } else {
      ENVOY_LOG_MISC(warn, "ALTS: Envoy's async gRPC client is unavailable, so handshakes will "
                           "block worker threads while they wait for the handshaker service");
    }
  }

  HandshakerFactory factory =
      [handshaker_service_address, is_upstream, alts_shared_state, handshaker_grpc_service,
       cluster_manager, stats_scope](Event::Dispatcher& dispatcher,
                                     const Network::Address::InstanceConstSharedPtr& local_address,
                                     const Network::Address::InstanceConstSharedPtr&,
                                     absl::string_view target_name) -> TsiHandshakerPtr {
    ASSERT(local_address != nullptr);
    std::unique_ptr<AltsTsiHandshaker> tsi_handshaker;
    if (handshaker_grpc_service != nullptr &&
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.alts_async_handshaker")) {
      absl::StatusOr<Grpc::RawAsyncClientSharedPtr> handshaker_client =
          cluster_manager->grpcAsyncClientManager().getOrCreateRawAsyncClientWithHashKey(
              *handshaker_grpc_service, *stats_scope, /*skip_cluster_check=*/true);
      if (handshaker_client.ok() && *handshaker_client != nullptr) {
        tsi_handshaker = AltsTsiHandshaker::createAsyncForServer(*std::move(handshaker_client));
      } else {
        ENVOY_LOG_EVERY_POW_2_MISC(warn,
                                   "ALTS: failed to get an async gRPC client to the handshaker "
                                   "service, using blocking calls instead: {}",
                                   handshaker_client.status().ToString());
      }
    }
    if (tsi_handshaker == nullptr) {
      // The handshaker makes blocking calls to the handshaker service.
      if (is_upstream) {
        tsi_handshaker =
            AltsTsiHandshaker::createForClient(alts_shared_state->getChannel(), target_name);
      } else {
        tsi_handshaker = AltsTsiHandshaker::createForServer(alts_shared_state->getChannel());
      }
    }
    return std::make_unique<TsiHandshaker>(std::move(tsi_handshaker), dispatcher);
  };

  return std::make_unique<TsiSocketFactory>(factory, validator);
}

} // namespace

ProtobufTypes::MessagePtr AltsTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::alts::v3::Alts>();
}

absl::StatusOr<Network::UpstreamTransportSocketFactoryPtr>
UpstreamAltsTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& factory_ctxt) {
  return createTransportSocketFactoryHelper<Network::UpstreamTransportSocketFactoryPtr>(
      message, /* is_upstream */ true, factory_ctxt);
}

absl::StatusOr<Network::DownstreamTransportSocketFactoryPtr>
DownstreamAltsTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& factory_ctxt,
    const std::vector<std::string>&) {
  return createTransportSocketFactoryHelper<Network::DownstreamTransportSocketFactoryPtr>(
      message, /* is_upstream */ false, factory_ctxt);
}

REGISTER_FACTORY(UpstreamAltsTransportSocketConfigFactory,
                 Server::Configuration::UpstreamTransportSocketConfigFactory);

REGISTER_FACTORY(DownstreamAltsTransportSocketConfigFactory,
                 Server::Configuration::DownstreamTransportSocketConfigFactory);

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
