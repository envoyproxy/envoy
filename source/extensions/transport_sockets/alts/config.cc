#include "source/extensions/transport_sockets/alts/config.h"

#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/google_grpc_context.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/transport_sockets/alts/grpc_tsi.h"
#include "source/extensions/transport_sockets/alts/tsi_socket.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// smart pointer for grpc_alts_credentials_options that will be automatically freed.
using GrpcAltsCredentialsOptionsPtr =
    CSmartPtr<grpc_alts_credentials_options, grpc_alts_credentials_options_destroy>;

namespace {

// TODO: gRPC v1.30.0-pre1 defines the equivalent function grpc_alts_set_rpc_protocol_versions
// that should be called directly when available.
void grpcAltsSetRpcProtocolVersions(grpc_gcp_rpc_protocol_versions* rpc_versions) {
  grpc_gcp_rpc_protocol_versions_set_max(rpc_versions, GRPC_PROTOCOL_VERSION_MAX_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MAX_MINOR);
  grpc_gcp_rpc_protocol_versions_set_min(rpc_versions, GRPC_PROTOCOL_VERSION_MIN_MAJOR,
                                         GRPC_PROTOCOL_VERSION_MIN_MINOR);
}

// Returns true if the peer's service account is found in peers, otherwise
// returns false and fills out err with an error message.
bool doValidate(const tsi_peer& peer, const absl::node_hash_set<std::string>& peers,
                TsiInfo& tsi_info, std::string& err) {
  for (size_t i = 0; i < peer.property_count; ++i) {
    const std::string name = std::string(peer.properties[i].name);
    const std::string value =
        std::string(peer.properties[i].value.data, peer.properties[i].value.length);
    if (name.compare(TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 0 &&
        peers.find(value) != peers.end()) {
      tsi_info.name_ = value;
      return true;
    }
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
    validator = [peers](const tsi_peer& peer, TsiInfo& tsi_info, std::string& err) {
      return doValidate(peer, peers, tsi_info, err);
    };
  }
  return validator;
}

// Manage ALTS singleton state via SingletonManager
class AltsSharedState : public Singleton::Instance {
public:
  AltsSharedState() { grpc_alts_shared_resource_dedicated_init(); }

  ~AltsSharedState() override { grpc_alts_shared_resource_dedicated_shutdown(); }

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
};

SINGLETON_MANAGER_REGISTRATION(alts_shared_state);

template <class TransportSocketFactoryPtr>
TransportSocketFactoryPtr createTransportSocketFactoryHelper(
    const Protobuf::Message& message, bool is_upstream,
    Server::Configuration::TransportSocketFactoryContext& factory_ctxt) {
  // A reference to this is held in the factory closure to keep the singleton
  // instance alive.
  auto alts_shared_state = factory_ctxt.singletonManager().getTyped<AltsSharedState>(
      SINGLETON_MANAGER_REGISTERED_NAME(alts_shared_state),
      [] { return std::make_shared<AltsSharedState>(); });
  auto config =
      MessageUtil::downcastAndValidate<const envoy::extensions::transport_sockets::alts::v3::Alts&>(
          message, factory_ctxt.messageValidationVisitor());
  HandshakeValidator validator = createHandshakeValidator(config);

  const std::string& handshaker_service = config.handshaker_service();
  HandshakerFactory factory =
      [handshaker_service, is_upstream,
       alts_shared_state](Event::Dispatcher& dispatcher,
                          const Network::Address::InstanceConstSharedPtr& local_address,
                          const Network::Address::InstanceConstSharedPtr&) -> TsiHandshakerPtr {
    ASSERT(local_address != nullptr);

    GrpcAltsCredentialsOptionsPtr options;
    if (is_upstream) {
      options = GrpcAltsCredentialsOptionsPtr(grpc_alts_credentials_client_options_create());
    } else {
      options = GrpcAltsCredentialsOptionsPtr(grpc_alts_credentials_server_options_create());
    }
    grpcAltsSetRpcProtocolVersions(&options->rpc_versions);
    const char* target_name = is_upstream ? "" : nullptr;
    tsi_handshaker* handshaker = nullptr;
    // Specifying target name as empty since TSI won't take care of validating peer identity
    // in this use case. The validation will be performed by TsiSocket with the validator.
    // Set the max frame size to 16KB.
    tsi_result status = alts_tsi_handshaker_create(
        options.get(), target_name, handshaker_service.c_str(), is_upstream,
        nullptr /* interested_parties */, &handshaker, 16384 /* default max frame size */);
    CHandshakerPtr handshaker_ptr{handshaker};

    if (status != TSI_OK) {
      const std::string handshaker_name = is_upstream ? "client" : "server";
      ENVOY_LOG_MISC(warn, "Cannot create ATLS {} handshaker, status: {}", handshaker_name, status);
      return nullptr;
    }

    return std::make_unique<TsiHandshaker>(std::move(handshaker_ptr), dispatcher);
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
