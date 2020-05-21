#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that inserts body to a header only request/response.
class AddBodyStreamFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "add-body-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    if (end_stream) {
      Buffer::OwnedImpl body("body");
      headers.setContentLength(body.length());
      decoder_callbacks_->addDecodedData(body, false);
    }

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    if (end_stream) {
      Buffer::OwnedImpl body("body");
      headers.setContentLength(body.length());
      encoder_callbacks_->addEncodedData(body, false);
    }

    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char AddBodyStreamFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<AddBodyStreamFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
} // namespace Envoy
