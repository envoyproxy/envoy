#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/connection_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

class OnSocketFailFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    close(fd());
    return Http::FilterHeadersStatus::Continue;
  }

  os_fd_t fd() {
    const Network::ConnectionImpl* impl =
        dynamic_cast<const Network::ConnectionImpl*>(decoder_callbacks_->connection());
    return impl->ioHandle().fdDoNotUse();
  }
};

class OnSocketFailFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  OnSocketFailFilterConfig() : EmptyHttpFilterConfig("on-socket-fail-filter") {}
  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::OnSocketFailFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<OnSocketFailFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy
