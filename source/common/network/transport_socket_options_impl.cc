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

void CommonTransportSocketFactory::hashKey(std::vector<uint8_t>& key,
                                           TransportSocketOptionsConstSharedPtr options) const {
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
}

TransportSocketOptionsConstSharedPtr TransportSocketOptionsUtility::fromFilterState(
    const StreamInfo::FilterStateSharedPtr& filter_state) {
  if (!filter_state) {
    return nullptr;
  }
  absl::string_view server_name;
  std::vector<std::string> application_protocols;
  std::vector<std::string> subject_alt_names;
  std::vector<std::string> alpn_fallback;
  absl::optional<Network::ProxyProtocolData> proxy_protocol_options;

  if (auto typed_data =
          filter_state->getDataReadOnly<UpstreamServerName>(UpstreamServerName::key());
      typed_data != nullptr) {
    server_name = typed_data->value();
  }

  if (auto typed_data = filter_state->getDataReadOnly<Network::ApplicationProtocols>(
          Network::ApplicationProtocols::key());
      typed_data != nullptr) {
    application_protocols = typed_data->value();
  }

  if (auto typed_data =
          filter_state->getDataReadOnly<UpstreamSubjectAltNames>(UpstreamSubjectAltNames::key());
      typed_data != nullptr) {
    subject_alt_names = typed_data->value();
  }

  if (auto typed_data =
          filter_state->getDataReadOnly<ProxyProtocolFilterState>(ProxyProtocolFilterState::key());
      typed_data != nullptr) {
    proxy_protocol_options.emplace(typed_data->value());
  }

  return std::make_shared<Network::TransportSocketOptionsImpl>(
      server_name, std::move(subject_alt_names), std::move(application_protocols),
      std::move(alpn_fallback), proxy_protocol_options, filter_state);
}

} // namespace Network
} // namespace Envoy
