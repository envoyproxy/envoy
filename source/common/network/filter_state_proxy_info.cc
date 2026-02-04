#include "source/common/network/filter_state_proxy_info.h"

#include "envoy/registry/registry.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/macros.h"
#include "source/common/network/utility.h"

#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Network {

const std::string& Http11ProxyInfoFilterState::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.network.transport_socket.http_11_proxy.info");
}

class Http11ProxyInfoFilterStateObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return Http11ProxyInfoFilterState::key(); }

  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    // Expected format: "<target_hostname>,<proxy_ip:port>"
    // Example: "example.com:443,127.0.0.1:15002"
    const std::vector<absl::string_view> parts = absl::StrSplit(data, absl::MaxSplits(',', 1));
    if (parts.size() != 2) {
      return nullptr;
    }

    const absl::string_view target = absl::StripAsciiWhitespace(parts[0]);
    const absl::string_view proxy = absl::StripAsciiWhitespace(parts[1]);
    if (target.empty() || proxy.empty()) {
      return nullptr;
    }

    auto proxy_address =
        Utility::parseInternetAddressAndPortNoThrow(std::string(proxy), /*v6only=*/true);
    if (proxy_address == nullptr) {
      return nullptr;
    }

    return std::make_unique<Http11ProxyInfoFilterState>(target, std::move(proxy_address));
  }
};

REGISTER_FACTORY(Http11ProxyInfoFilterStateObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
