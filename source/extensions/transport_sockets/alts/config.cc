#include "extensions/transport_sockets/alts/config.h"

#include "envoy/config/transport_socket/alts/v2alpha/alts.pb.h"
#include "envoy/config/transport_socket/alts/v2alpha/alts.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/transport_sockets/alts/grpc_tsi.h"
#include "extensions/transport_sockets/alts/tsi_socket.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

// smart pointer for grpc_alts_credentials_options that will be automatically freed.
typedef CSmartPtr<grpc_alts_credentials_options, grpc_alts_credentials_options_destroy>
    GrpcAltsCredentialsOptionsPtr;

namespace {

// Returns true if the peer's service account is found in peers, otherwise
// returns false and fills out err with an error message.
bool doValidate(const tsi_peer& peer, const std::unordered_set<std::string>& peers,
                std::string& err) {
  for (size_t i = 0; i < peer.property_count; ++i) {
    const std::string name = std::string(peer.properties[i].name);
    const std::string value =
        std::string(peer.properties[i].value.data, peer.properties[i].value.length);
    if (name.compare(TSI_ALTS_SERVICE_ACCOUNT_PEER_PROPERTY) == 0 &&
        peers.find(value) != peers.end()) {
      return true;
    }
  }

  err =
      "Couldn't find peer's service account in peer_service_accounts: " + absl::StrJoin(peers, ",");
  return false;
}

HandshakeValidator
createHandshakeValidator(const envoy::config::transport_socket::alts::v2alpha::Alts& config) {
  const auto& peer_service_accounts = config.peer_service_accounts();
  const std::unordered_set<std::string> peers(peer_service_accounts.cbegin(),
                                              peer_service_accounts.cend());
  HandshakeValidator validator;
  // Skip validation if peers is empty.
  if (!peers.empty()) {
    validator = [peers](const tsi_peer& peer, std::string& err) {
      return doValidate(peer, peers, err);
    };
  }
  return validator;
}

Network::TransportSocketFactoryPtr
createTransportSocketFactoryHelper(const Protobuf::Message& message, bool is_upstream) {
  auto config =
      MessageUtil::downcastAndValidate<const envoy::config::transport_socket::alts::v2alpha::Alts&>(
          message);
  HandshakeValidator validator = createHandshakeValidator(config);

  const std::string handshaker_service = config.handshaker_service();
  HandshakerFactory factory =
      [handshaker_service,
       is_upstream](Event::Dispatcher& dispatcher,
                    const Network::Address::InstanceConstSharedPtr& local_address,
                    const Network::Address::InstanceConstSharedPtr&) -> TsiHandshakerPtr {
    ASSERT(local_address != nullptr);

    GrpcAltsCredentialsOptionsPtr options;
    if (is_upstream) {
      options = GrpcAltsCredentialsOptionsPtr(grpc_alts_credentials_client_options_create());
    } else {
      options = GrpcAltsCredentialsOptionsPtr(grpc_alts_credentials_server_options_create());
    }
    const char* target_name = is_upstream ? "" : nullptr;
    tsi_handshaker* handshaker = nullptr;
    // Specifying target name as empty since TSI won't take care of validating peer identity
    // in this use case. The validation will be performed by TsiSocket with the validator.
    tsi_result status = alts_tsi_handshaker_create(
        options.get(), target_name, handshaker_service.c_str(), is_upstream, &handshaker);
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
  return std::make_unique<envoy::config::transport_socket::alts::v2alpha::Alts>();
}

Network::TransportSocketFactoryPtr
UpstreamAltsTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext&) {
  return createTransportSocketFactoryHelper(message, /* is_upstream */ true);
}

Network::TransportSocketFactoryPtr
DownstreamAltsTransportSocketConfigFactory::createTransportSocketFactory(
    const Protobuf::Message& message, Server::Configuration::TransportSocketFactoryContext&,
    const std::vector<std::string>&) {
  return createTransportSocketFactoryHelper(message, /* is_upstream */ false);
}

static Registry::RegisterFactory<UpstreamAltsTransportSocketConfigFactory,
                                 Server::Configuration::UpstreamTransportSocketConfigFactory>
    upstream_registered_;

static Registry::RegisterFactory<DownstreamAltsTransportSocketConfigFactory,
                                 Server::Configuration::DownstreamTransportSocketConfigFactory>
    downstream_registered_;

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
