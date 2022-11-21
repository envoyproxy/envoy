#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/filter_state_proxy_info.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// A filter that looks for a specific header, and takes the address from that
// header and inserts proxy override metadata.
class HeaderToProxyFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    auto connect_proxy = Http::LowerCaseString("connect-proxy");
    auto hostname = request_headers.getHostValue();
    ASSERT(!hostname.empty());
    if (!request_headers.get(connect_proxy).empty()) {
      std::string address_string(request_headers.get(connect_proxy)[0]->value().getStringView());
      auto address = Network::Utility::parseInternetAddressAndPort(address_string);
      decoder_callbacks_->streamInfo().filterState()->setData(
          Network::Http11ProxyInfoFilterState::key(),
          std::make_unique<Network::Http11ProxyInfoFilterState>(hostname, address),
          StreamInfo::FilterState::StateType::ReadOnly,
          StreamInfo::FilterState::LifeSpan::FilterChain);
      request_headers.remove(connect_proxy);
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

class HeaderToProxyFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  HeaderToProxyFilterConfig() : EmptyHttpFilterConfig("header-to-proxy-filter") {}

  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::HeaderToProxyFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<HeaderToProxyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
