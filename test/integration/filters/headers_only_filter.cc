#include <string>

#include "envoy/registry/registry.h"
#include "envoy/http/filter.h"
#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/common/empty_http_filter_config.h"

#include "envoy/server/filter_config.h"

namespace Envoy {

// DRYs up the creation of a simple filter config for a filter that requires no config.
// TODO(snowp): make this reusable and use for other test filters.
template<class FilterT>
class SimpleFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
  public:
    SimpleFilterConfig() : EmptyHttpFilterConfig(FilterT::name) {}

    Http::FilterFactoryCb createFilter(const std::string&, Server::Configuration::FactoryContext&) {
      return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<FilterT>());
      };
    }
};

class HeaderOnlyDecoderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "decode-headers-only";

  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::ContinueAndEndStream;
  }
};

constexpr char HeaderOnlyDecoderFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<HeaderOnlyDecoderFilter>, Server::Configuration::NamedHttpFilterConfigFactory> decoder_register_;

class HeaderOnlyEncoderFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "encode-headers-only";

  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::ContinueAndEndStream;
  }
};

constexpr char HeaderOnlyEncoderFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<HeaderOnlyEncoderFilter>, Server::Configuration::NamedHttpFilterConfigFactory> encoder_register_;
}
