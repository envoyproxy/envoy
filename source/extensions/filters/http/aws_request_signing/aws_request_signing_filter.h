#pragma once

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/common/aws/signer.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

/**
 * All stats for the AWS request signing filter. @see stats_macros.h
 */
// clang-format off
#define ALL_AWS_REQUEST_SIGNING_FILTER_STATS(COUNTER)                                                           \
  COUNTER(signing_added)                                                                        \
  COUNTER(signing_failed)
// clang-format on

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_AWS_REQUEST_SIGNING_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Abstract filter configuration.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  /**
   * @return the config's signer.
   */
  virtual Extensions::Common::Aws::Signer& signer() const PURE;

  /**
   * @return the filter stats.
   */
  virtual FilterStats& stats() const PURE;

  /**
   * @return the host rewrite value.
   */
  virtual const std::string& hostRewrite() const PURE;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * Configuration for the AWS request signing filter.
 */
class FilterConfigImpl : public FilterConfig {
public:
  FilterConfigImpl(Extensions::Common::Aws::SignerPtr&& signer, const std::string& stats_prefix,
                   Stats::Scope& scope, const std::string& host_rewrite);

  Extensions::Common::Aws::Signer& signer() const override;
  FilterStats& stats() const override;
  const std::string& hostRewrite() const override;

private:
  // TODO(rgs1): Signer::sign() should be const.
  mutable Extensions::Common::Aws::SignerPtr signer_;
  mutable FilterStats stats_;
  std::string host_rewrite_;
};

/**
 * HTTP AWS request signing auth filter.
 */
class Filter : public Http::PassThroughDecoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const std::shared_ptr<FilterConfig>& config);

  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const FilterConfig* getConfig() const;

  std::shared_ptr<FilterConfig> config_;
  mutable const FilterConfig* effective_config_{nullptr};
};

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
