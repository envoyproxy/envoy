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
    // Expected format: "<target_host:port>,<proxy_ip:port>"
    // Example: "example.com:443,127.0.0.1:15002" or "example.com:443,[::1]:15002"
    const std::vector<absl::string_view> parts = absl::StrSplit(data, absl::MaxSplits(',', 1));
    if (parts.size() != 2) {
      ENVOY_LOG_MISC(debug,
                     "Invalid filter state '{}': expected '<target_host:port>,<proxy_ip:port>', "
                     "missing comma separator; value='{}'",
                     Http11ProxyInfoFilterState::key(), std::string(data));
      return nullptr;
    }

    const absl::string_view target = absl::StripAsciiWhitespace(parts[0]);
    const absl::string_view proxy = absl::StripAsciiWhitespace(parts[1]);
    if (target.empty() || proxy.empty()) {
      ENVOY_LOG_MISC(debug,
                     "Invalid filter state '{}': empty target/proxy after trimming "
                     "(target_empty={}, proxy_empty={}); value='{}'",
                     Http11ProxyInfoFilterState::key(), target.empty(), proxy.empty(),
                     std::string(data));
      return nullptr;
    }

    // v6only=true is intentional: if `proxy` is provided as a bracketed IPv6 literal
    // ("[...]:port"), treat it as *real* IPv6 and avoid IPv4-mapped IPv6 semantics (e.g.
    // "[::ffff:1.1.1.1]:1234"). If the proxy is IPv4, it should be encoded explicitly as
    // "a.b.c.d:port" in the filter-state.
    auto proxy_address =
        Utility::parseInternetAddressAndPortNoThrow(std::string(proxy), /*v6only=*/true);
    if (proxy_address == nullptr) {
      ENVOY_LOG_MISC(debug,
                     "Invalid filter state '{}': could not parse proxy ip:port (IPv6 must use "
                     "bracket notation); proxy='{}'",
                     Http11ProxyInfoFilterState::key(), std::string(proxy));
      return nullptr;
    }

    return std::make_unique<Http11ProxyInfoFilterState>(target, std::move(proxy_address));
  }
};

REGISTER_FACTORY(Http11ProxyInfoFilterStateObjectFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace Network
} // namespace Envoy
