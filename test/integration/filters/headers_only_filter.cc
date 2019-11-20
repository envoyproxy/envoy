#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/integration/filters/common.h"

namespace Envoy {

class HeaderOnlyDecoderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-only";

  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::ContinueAndEndStream;
  }
};

constexpr char HeaderOnlyDecoderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<HeaderOnlyDecoderFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    decoder_register_;

class HeaderOnlyEncoderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-headers-only";

  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::ContinueAndEndStream;
  }
};

constexpr char HeaderOnlyEncoderFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<HeaderOnlyEncoderFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
} // namespace Envoy
