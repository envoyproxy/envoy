#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/http/header_utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Faulty filter that may remove critical headers.
class InvalidHeaderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "invalid-header-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    // Remove method when there is a "remove-method" header.
    if (!headers.get(Http::LowerCaseString("remove-method")).empty()) {
      headers.removeMethod();
    }
    if (!headers.get(Http::LowerCaseString("remove-path")).empty()) {
      headers.removePath();
    }
    if (Http::HeaderUtility::isConnect(headers)) {
      headers.removeHost();
    }
    if (!headers.get(Http::LowerCaseString("send-reply")).empty()) {
      decoder_callbacks_->sendLocalReply(Envoy::Http::Code::OK, "", nullptr, absl::nullopt,
                                         "invalid_header_filter_ready");
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char InvalidHeaderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<InvalidHeaderFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    decoder_register_;
constexpr char InvalidHeaderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<InvalidHeaderFilter>,
                                 Server::Configuration::UpstreamHttpFilterConfigFactory>
    decoder_register_upstream_;

} // namespace Envoy
