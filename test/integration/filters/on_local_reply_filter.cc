#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

namespace Envoy {

class OnLocalReplyFilter : public Http::PassThroughFilter {
public:
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& request_headers, bool) override {
    if (!request_headers.get(Http::LowerCaseString("reset")).empty()) {
      reset_ = true;
    }
    if (!request_headers.get(Http::LowerCaseString("dual-local-reply")).empty()) {
      dual_reply_ = true;
    }
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "original_reply", nullptr,
                                       absl::nullopt, "original_reply");
    return Http::FilterHeadersStatus::StopIteration;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    if (dual_reply_) {
      decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "second_reply", nullptr,
                                         absl::nullopt, "second_reply");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override {
    if (reset_) {
      return Http::LocalErrorStatus::ContinueAndResetStream;
    }
    return Http::LocalErrorStatus::Continue;
  }

  bool reset_{};
  bool dual_reply_{};
};

class OnLocalReplyFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpDualFilterConfig {
public:
  OnLocalReplyFilterConfig() : EmptyHttpDualFilterConfig("on-local-reply-filter") {}
  absl::StatusOr<Http::FilterFactoryCb>
  createDualFilter(const std::string&, Server::Configuration::ServerFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(std::make_shared<::Envoy::OnLocalReplyFilter>());
    };
  }
};

// perform static registration
static Registry::RegisterFactory<OnLocalReplyFilterConfig,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<OnLocalReplyFilterConfig,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
