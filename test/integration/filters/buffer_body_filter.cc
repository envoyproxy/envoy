#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// A test filter that passes header through but buffers body.
class BufferBodyFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "buffer-body-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override {
    ASSERT(!end_stream);
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override {
    if (end_stream) {
      return Http::FilterDataStatus::Continue;
    }
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override {
    ASSERT(!end_stream);
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance&, bool end_stream) override {
    if (end_stream) {
      return Http::FilterDataStatus::Continue;
    }
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
};

constexpr char BufferBodyFilter::name[];

static Registry::RegisterFactory<SimpleFilterConfig<BufferBodyFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    encoder_register_;
} // namespace Envoy
