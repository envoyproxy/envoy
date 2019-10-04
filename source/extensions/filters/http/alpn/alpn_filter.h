#pragma once

#include "envoy/config/filter/http/alpn/v2alpha/alpn.pb.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Alpn {

using AlpnOverride = absl::flat_hash_map<Http::Protocol, std::vector<std::string>>;

class AlpnFilterConfig {
public:
  AlpnFilterConfig() = default;

  explicit AlpnFilterConfig(
      const envoy::config::filter::http::alpn::v2alpha::FilterConfig& proto_config);

  AlpnOverride& getAlpnOverride() { return alpn_override_; }

private:
  Http::Protocol getHttpProtocol(
      const envoy::config::filter::http::alpn::v2alpha::FilterConfig::Protocol& protocol);

  AlpnOverride alpn_override_;
};

using AlpnFilterConfigSharedPtr = std::shared_ptr<AlpnFilterConfig>;

class AlpnFilter : public Http::PassThroughDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  explicit AlpnFilter(const AlpnFilterConfigSharedPtr& config) : config_(config) {}

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;

private:
  const AlpnFilterConfigSharedPtr config_;
};

} // namespace Alpn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
