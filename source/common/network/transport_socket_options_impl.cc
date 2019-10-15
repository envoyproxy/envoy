#include "common/network/transport_socket_options_impl.h"

#include "common/common/scalar_to_byte_vector.h"
#include "common/common/utility.h"
#include "common/network/application_protocol.h"
#include "common/network/upstream_server_name.h"

namespace Envoy {
namespace Network {
void TransportSocketOptionsImpl::hashKey(std::vector<uint8_t>& key) const {
  if (override_server_name_.has_value()) {
    pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(override_server_name_.value()), key);
  }

  if (!override_alpn_list_.empty()) {
    for (const auto& protocol : override_alpn_list_) {
      pushScalarToByteVector(StringUtil::CaseInsensitiveHash()(protocol), key);
    }
  }
}

TransportSocketOptionsSharedPtr
TransportSocketOptionsUtility::fromFilterState(const StreamInfo::FilterState& filter_state) {
  absl::string_view server_name;
  std::vector<std::string> application_protocols;
  bool needs_transport_socket_options = false;
  if (filter_state.hasData<UpstreamServerName>(UpstreamServerName::key())) {
    const auto& upstream_server_name =
        filter_state.getDataReadOnly<UpstreamServerName>(UpstreamServerName::key());
    server_name = upstream_server_name.value();
    needs_transport_socket_options = true;
  }

  if (filter_state.hasData<Network::ApplicationProtocols>(Network::ApplicationProtocols::key())) {
    const auto& alpn = filter_state.getDataReadOnly<Network::ApplicationProtocols>(
        Network::ApplicationProtocols::key());
    application_protocols = alpn.value();
    needs_transport_socket_options = true;
  }

  if (needs_transport_socket_options) {
    return std::make_shared<Network::TransportSocketOptionsImpl>(
        server_name, std::vector<std::string>{}, std::vector<std::string>{application_protocols});
  } else {
    return nullptr;
  }
}

} // namespace Network
} // namespace Envoy
