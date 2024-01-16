#include "source/extensions/transport_sockets/alts/config.h"

#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/google_grpc_context.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"
#include "source/extensions/transport_sockets/alts/alts_tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/grpc_tsi.h"
#include "source/extensions/transport_sockets/alts/tsi_handshaker.h"
#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "absl/container/node_hash_set.h"
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
  if (peers.find(tsi_info.peer_identity_) != peers.end()) {
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
  // instance alive.
  auto alts_shared_state =
      factory_ctxt.serverFactoryContext().singletonManager().getTyped<AltsSharedState>(
          SINGLETON_MANAGER_REGISTERED_NAME(alts_shared_state), [handshaker_service_address] {
            return std::make_shared<AltsSharedState>(handshaker_service_address);
          });
  HandshakerFactory factory =
      [handshaker_service_address, is_upstream,
       alts_shared_state](Event::Dispatcher& dispatcher,
                          const Network::Address::InstanceConstSharedPtr& local_address,
                          const Network::Address::InstanceConstSharedPtr&) -> TsiHandshakerPtr {
    ASSERT(local_address != nullptr);
    std::unique_ptr<AltsTsiHandshaker> tsi_handshaker;
    if (is_upstream) {
      tsi_handshaker = AltsTsiHandshaker::createForClient(alts_shared_state->getChannel());
    } else {
      tsi_handshaker = AltsTsiHandshaker::createForServer(alts_shared_state->getChannel());
    }
    return std::make_unique<TsiHandshaker>(std::move(tsi_handshaker), dispatcher);
  };

  return std::make_unique<TsiSocketFactory>(factory, validator);
}

} // namespace

ProtobufTypes::MessagePtr AltsTransportSocketConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::transport_sockets::alts::v3::Alts>();
}

Network::UpstreamTransportSocketFactoryPtr
UpstreamAltsTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message,
    Server::Configuration::TransportSocketFactoryContext& factory_ctxt) {
  return createTransportSocketFactoryHelper<Network::UpstreamTransportSocketFactoryPtr>(
      message, /* is_upstream */ true, factory_ctxt);
}

Network::DownstreamTransportSocketFactoryPtr
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
