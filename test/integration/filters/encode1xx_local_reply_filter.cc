#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

// Test filter for local reply on the encode 1xx path.
class Encode1xxLocalReplyFilter : public Http::PassThroughFilter {
public:
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    encoder_callbacks_->sendLocalReply(Http::Code::InternalServerError,
                                       "Local Reply During encode1xxHeaders.", nullptr,
                                       absl::nullopt, "");
    return Http::Filter1xxHeadersStatus::Continue;
  }
};

class Encode1xxLocalReplyFilterConfig
    : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  Encode1xxLocalReplyFilterConfig() : EmptyHttpFilterConfig("encode1xx-local-reply-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<Encode1xxLocalReplyFilter>());
    };
  }
};

static Registry::RegisterFactory<Encode1xxLocalReplyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
