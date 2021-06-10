#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

// A filter that only calls Http::FilterHeadersStatus::Continue after a local reply.
class ContinueAfterLocalReplyFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "continue-after-local-reply-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                       "ContinueAfterLocalReplyFilter is ready");
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char ContinueAfterLocalReplyFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<ContinueAfterLocalReplyFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
