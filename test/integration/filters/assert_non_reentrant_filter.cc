#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that sends a local reply via the filter chain asserting that
// the filter is not concurrently invoked by the encoder and decoder filter
// chain.
class AssertNonReentrantFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "assert-non-reentrant-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    ASSERT(!is_active_);
    is_active_ = true;
    decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable,
                                       "AssertNonReentrantFilter local reply during decodeHeaders.",
                                       nullptr, absl::nullopt, "");
    is_active_ = false;
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    ASSERT(!is_active_);
    return Http::FilterHeadersStatus::Continue;
  }

private:
  // Tracks whether the filter is active.
  bool is_active_{false};
};

constexpr char AssertNonReentrantFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<AssertNonReentrantFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
static Registry::RegisterFactory<SimpleFilterConfig<AssertNonReentrantFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    encoder_register_upstream_;
} // namespace Envoy
