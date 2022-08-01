#include "source/common/network/transport_socket_options_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/hashable.h"

#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_subject_alt_names.h"

namespace Envoy {
namespace Network {

void CommonUpstreamTransportSocketFactory::hashKey(
    std::vector<uint8_t>& key, TransportSocketOptionsConstSharedPtr options) const {
  if (!options) {
    return;
  }
  const auto& server_name_overide = options->serverNameOverride();
  if (server_name_overide.has_value()) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(server_name_overide.value()), key);
  }

  const auto& verify_san_list = options->verifySubjectAltNameListOverride();
  for (const auto& san : verify_san_list) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(san), key);
  }

  const auto& alpn_list = options->applicationProtocolListOverride();
  for (const auto& protocol : alpn_list) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(protocol), key);
  }

  const auto& alpn_fallback = options->applicationProtocolFallback();
  for (const auto& protocol : alpn_fallback) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(protocol), key);
  }

  for (const auto& object : options->downstreamSharedFilterStateObjects()) {
    if (auto hashable = dynamic_cast<const Hashable*>(object.data_.get()); hashable != nullptr) {
      if (auto hash = hashable->hash(); hash) {
        pushScalarToByteVector(hash.value(), key);
      }
    }
  }
}

TransportSocketOptionsConstSharedPtr
TransportSocketOptionsUtility::fromFilterState(const StreamInfo::FilterState& filter_state) {
  absl::string_view server_name;
  std::vector<std::string> application_protocols;
  std::vector<std::string> subject_alt_names;
  std::vector<std::string> alpn_fallback;
  absl::optional<Network::ProxyProtocolData> proxy_protocol_options;
  std::unique_ptr<const TransportSocketOptions::Http11ProxyInfo> proxy_info;

  bool needs_transport_socket_options = false;
  if (auto typed_data = filter_state.getDataReadOnly<UpstreamServerName>(UpstreamServerName::key());
      typed_data != nullptr) {
    server_name = typed_data->value();
    needs_transport_socket_options = true;
  }

  if (auto typed_data = filter_state.getDataReadOnly<Network::ApplicationProtocols>(
          Network::ApplicationProtocols::key());
      typed_data != nullptr) {
    application_protocols = typed_data->value();
    needs_transport_socket_options = true;
  }

  if (auto typed_data =
          filter_state.getDataReadOnly<UpstreamSubjectAltNames>(UpstreamSubjectAltNames::key());
      typed_data != nullptr) {
    subject_alt_names = typed_data->value();
    needs_transport_socket_options = true;
  }

  if (auto typed_data =
          filter_state.getDataReadOnly<ProxyProtocolFilterState>(ProxyProtocolFilterState::key());
      typed_data != nullptr) {
    proxy_protocol_options.emplace(typed_data->value());
    needs_transport_socket_options = true;
  }

  if (auto typed_data = filter_state.getDataReadOnly<Http11ProxyInfoFilterState>(
          Http11ProxyInfoFilterState::key());
      typed_data != nullptr) {
    proxy_info = std::make_unique<TransportSocketOptions::Http11ProxyInfo>(typed_data->hostname(),
                                                                           typed_data->address());
    needs_transport_socket_options = true;
  }

  StreamInfo::FilterState::ObjectsPtr objects = filter_state.objectsSharedWithUpstreamConnection();
  if (!objects->empty()) {
    needs_transport_socket_options = true;
  }

  if (needs_transport_socket_options) {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        server_name, std::move(subject_alt_names), std::move(application_protocols),
        std::move(alpn_fallback), proxy_protocol_options, std::move(objects),
        std::move(proxy_info));
  } else {
    return nullptr;
  }
}

} // namespace Network
} // namespace Envoy
