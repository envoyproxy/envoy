#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/stream_info/uint64_accessor_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

#include "gtest/gtest.h"

namespace Envoy {

using StreamInfo::UInt64AccessorImpl;

// A filter that sticks stream info into headers for integration testing.
class StreamInfoToHeadersFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "stream-info-to-headers-filter";

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override {
    const std::string dns_start = "envoy.dynamic_forward_proxy.dns_start_ms";
    const std::string dns_end = "envoy.dynamic_forward_proxy.dns_end_ms";
    StreamInfo::StreamInfo& stream_info = decoder_callbacks_->streamInfo();

    if (stream_info.filterState()->hasData<UInt64AccessorImpl>(dns_start)) {
      headers.addCopy(
          Http::LowerCaseString("dns_start"),
          absl::StrCat(
              stream_info.filterState()->getDataReadOnly<UInt64AccessorImpl>(dns_start).value()));
    }
    if (stream_info.filterState()->hasData<UInt64AccessorImpl>(dns_end)) {
      headers.addCopy(
          Http::LowerCaseString("dns_end"),
          absl::StrCat(
              stream_info.filterState()->getDataReadOnly<UInt64AccessorImpl>(dns_end).value()));
    }
    return Http::FilterHeadersStatus::Continue;
  }
};

constexpr char StreamInfoToHeadersFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<StreamInfoToHeadersFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
