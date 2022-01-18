#include "source/common/network/transport_socket_options_impl.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"
#include "source/common/network/application_protocol.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/upstream_server_name.h"
#include "source/common/network/upstream_subject_alt_names.h"

namespace Envoy {
namespace Network {
namespace {
void commonHashKey(const TransportSocketOptions& options, std::vector<std::uint8_t>& key,
                   const Network::TransportSocketFactory& factory) {
  const auto& server_name_overide = options.serverNameOverride();
  if (server_name_overide.has_value()) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(server_name_overide.value()), key);
  }

  const auto& verify_san_list = options.verifySubjectAltNameListOverride();
  for (const auto& san : verify_san_list) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(san), key);
  }

  const auto& alpn_list = options.applicationProtocolListOverride();
  for (const auto& protocol : alpn_list) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(protocol), key);
  }

  const auto& alpn_fallback = options.applicationProtocolFallback();
  for (const auto& protocol : alpn_fallback) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(protocol), key);
  }

  // Proxy protocol options should only be included in the hash if the upstream
  // socket intends to use them.
  const auto& proxy_protocol_options = options.proxyProtocolOptions();
  if (proxy_protocol_options.has_value() && factory.usesProxyProtocolOptions()) {
    pushScalarToByteVector(
        StringUtil::CaseInsensitiveHash()(proxy_protocol_options.value().asStringForHash()), key);
  }
}
} // namespace

void AlpnDecoratingTransportSocketOptions::hashKey(
    std::vector<uint8_t>& key, const Network::TransportSocketFactory& factory) const {
  commonHashKey(*this, key, factory);
}

void TransportSocketOptionsImpl::hashKey(std::vector<uint8_t>& key,
                                         const Network::TransportSocketFactory& factory) const {
  commonHashKey(*this, key, factory);
}

TransportSocketOptionsConstSharedPtr
TransportSocketOptionsUtility::fromFilterState(const StreamInfo::FilterState& filter_state) {
  absl::string_view server_name;
  std::vector<std::string> application_protocols;
  std::vector<std::string> subject_alt_names;
  std::vector<std::string> alpn_fallback;
  absl::optional<Network::ProxyProtocolData> proxy_protocol_options;

  bool needs_transport_socket_options = false;
  if (const auto* state = filter_state.getDataReadOnlyGeneric(UpstreamServerName::key());
      state != nullptr) {
    if (const auto* typed_state = dynamic_cast<const UpstreamServerName*>(state);
        typed_state != nullptr) {
      server_name = typed_state->value();
      needs_transport_socket_options = true;
    }
  }

  if (const auto* state = filter_state.getDataReadOnlyGeneric(Network::ApplicationProtocols::key());
      state != nullptr) {
    if (const auto* typed_state = dynamic_cast<const Network::ApplicationProtocols*>(state);
        typed_state != nullptr) {
      application_protocols = typed_state->value();
      needs_transport_socket_options = true;
    }
  }

  if (const auto* state = filter_state.getDataReadOnlyGeneric(UpstreamSubjectAltNames::key());
      state != nullptr) {
    if (const auto* typed_state = dynamic_cast<const UpstreamSubjectAltNames*>(state);
        typed_state != nullptr) {
      subject_alt_names = typed_state->value();
      needs_transport_socket_options = true;
    }
  }

  if (const auto* state = filter_state.getDataReadOnlyGeneric(ProxyProtocolFilterState::key());
      state != nullptr) {
    if (const auto* typed_state = dynamic_cast<const ProxyProtocolFilterState*>(state);
        typed_state != nullptr) {
      proxy_protocol_options.emplace(typed_state->value());
      needs_transport_socket_options = true;
    }
  }

  if (needs_transport_socket_options) {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        server_name, std::move(subject_alt_names), std::move(application_protocols),
        std::move(alpn_fallback), proxy_protocol_options);
  } else {
    return nullptr;
  }
}

} // namespace Network
} // namespace Envoy
