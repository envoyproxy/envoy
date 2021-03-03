#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

class OnLocalReplyFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    if (!request_headers.get(Http::LowerCaseString("reset")).empty()) {
      reset_ = true;
    }
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "body", nullptr, absl::nullopt,
                                       "details");
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override {
    if (reset_) {
      return Http::LocalErrorStatus::ContinueAndResetStream;
    }
    return Http::LocalErrorStatus::Continue;
  }

  bool reset_{};
};

class OnLocalReplyFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  OnLocalReplyFilterConfig() : EmptyHttpFilterConfig("on-local-reply-filter") {}
  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::OnLocalReplyFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<OnLocalReplyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy
