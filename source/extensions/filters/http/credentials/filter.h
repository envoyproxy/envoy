#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/matchers.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/extensions/filters/http/credentials/v3alpha/injector.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/rest_api_fetcher.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include "source/extensions/filters/http/credentials/source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

/**
 * All stats for the Credentials Injector filter. @see stats_macros.h
 */
#define ALL_CREDENTIAL_FILTER_STATS(COUNTER)                                                            \
  COUNTER(credential_injected)                                                                          \
  COUNTER(credential_error)

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_CREDENTIAL_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * This class encapsulates all data needed for the filter to operate so that we don't pass around
 * raw protobufs and other arbitrary data.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::credentials::v3alpha::Injector& proto_config,
               CredentialSourcePtr credential_source,
               Stats::Scope& scope,
               const std::string& stats_prefix);
  FilterStats& stats() { return stats_; }
  CredentialSource& credentialSource() const { return *credential_source_; }

private:
  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  FilterStats stats_;
  const CredentialSourcePtr credential_source_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * The filter is the primary entry point for the OAuth workflow. Its responsibilities are to
 * receive incoming requests and decide at what state of the OAuth workflow they are in. Logic
 * beyond that is broken into component classes.
 */
class Filter : public Http::PassThroughDecoderFilter, public CredentialSource::Callbacks {
public:
  Filter(FilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  // CredentialSource::Callbacks
  void onSuccess(std::string credential) override;
  void onFailure() override;

private:
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::RequestHeaderMap* request_headers_{};

  FilterConfigSharedPtr config_;

  // Tracks any outstanding in-flight requests, allowing us to cancel the request
  // if the filter ends before the request completes.
  CredentialSource::RequestPtr in_flight_credential_request_;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
