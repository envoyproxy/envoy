#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that continues iteration of headers-only request/response without ending the
// stream, then injects a body later.
class ContinueHeadersOnlyInjectBodyFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "continue-headers-only-inject-body-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    headers.setContentLength(body_.length());
    decoder_callbacks_->dispatcher().post([this]() -> void {
      Buffer::OwnedImpl buffer(body_);
      decoder_callbacks_->injectDecodedDataToFilterChain(buffer, true);
    });
    return Http::FilterHeadersStatus::ContinueAndDontEndStream;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    headers.setContentLength(body_.length());
    encoder_callbacks_->dispatcher().post([this]() -> void {
      Buffer::OwnedImpl buffer(body_);
      encoder_callbacks_->injectEncodedDataToFilterChain(buffer, true);
    });
    return Http::FilterHeadersStatus::ContinueAndDontEndStream;
  }

private:
  constexpr static absl::string_view body_ = "body";
};

static Registry::RegisterFactory<SimpleFilterConfig<ContinueHeadersOnlyInjectBodyFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;
} // namespace Envoy
