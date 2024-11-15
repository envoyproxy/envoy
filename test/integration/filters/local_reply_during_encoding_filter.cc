#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

class LocalReplyDuringEncode : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "local-reply-during-encode";

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    encoder_callbacks_->sendLocalReply(Http::Code::InternalServerError, "", nullptr, absl::nullopt,
                                       "");
    return Http::FilterHeadersStatus::StopIteration;
  }
};

constexpr char LocalReplyDuringEncode::name[];
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringEncode>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
static Registry::RegisterFactory<SimpleFilterConfig<LocalReplyDuringEncode>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    register_upstream_;

} // namespace Envoy
