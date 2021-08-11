#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/aws/signer.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

class Arn {
public:
  Arn(absl::string_view partition, absl::string_view service, absl::string_view region,
      absl::string_view account_id, absl::string_view resource_type,
      absl::string_view function_name)
      : partition_(partition), service_(service), region_(region), account_id_(account_id),
        resource_type_(resource_type), function_name_(function_name) {}

  const std::string& partition() const { return partition_; }
  const std::string& service() const { return service_; }
  const std::string& region() const { return region_; }
  const std::string& accountId() const { return account_id_; }
  const std::string& resourceType() const { return resource_type_; }
  const std::string& functionName() const { return function_name_; }

private:
  std::string partition_;
  std::string service_;
  std::string region_;
  std::string account_id_;
  std::string resource_type_;
  std::string function_name_; // resource_id
};

/**
 * Parses the input string into a structured ARN.
 *
 * The format is expected to be as such:
 * arn:partition:service:region:account-id:resource-type:resource-id
 *
 * Lambda ARN Example:
 * arn:aws:lambda:us-west-2:987654321:function:hello_envoy
 */
absl::optional<Arn> parseArn(absl::string_view arn);

/**
 * All stats for the AWS Lambda filter. @see stats_macros.h
 */
#define ALL_AWS_LAMBDA_FILTER_STATS(COUNTER, HISTOGRAM)                                            \
  COUNTER(server_error)                                                                            \
  HISTOGRAM(upstream_rq_payload_size, Bytes)

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_AWS_LAMBDA_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

/**
 * Lambda invocation mode.
 * Synchronous mode is analogous to a blocking call; Lambda responds when it's completed processing.
 * In the Asynchronous mode, Lambda responds immediately acknowledging it received the request.
 */
enum class InvocationMode { Synchronous, Asynchronous };

class FilterSettings : public Router::RouteSpecificFilterConfig {
public:
  FilterSettings(const Arn& arn, InvocationMode mode, bool payload_passthrough)
      : arn_(arn), invocation_mode_(mode), payload_passthrough_(payload_passthrough) {}

  const Arn& arn() const& { return arn_; }
  bool payloadPassthrough() const { return payload_passthrough_; }
  InvocationMode invocationMode() const { return invocation_mode_; }

private:
  Arn arn_;
  InvocationMode invocation_mode_;
  bool payload_passthrough_;
};

class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {

public:
  Filter(const FilterSettings& config, const FilterStats& stats,
         const std::shared_ptr<Extensions::Common::Aws::Signer>& sigv4_signer);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  /**
   * Calculates the function ARN, value of pass-through, etc. by checking per-filter configurations
   * and general filter configuration. Ultimately, the most specific configuration wins.
   * @return error message if settings are invalid. Otherwise, empty string.
   */
  void resolveSettings();
  FilterStats& stats() { return stats_; }

  /**
   * Used for unit testing only
   */
  const FilterSettings& settingsForTest() const { return settings_; }

private:
  absl::optional<FilterSettings> getRouteSpecificSettings() const;
  // Convert the HTTP request to JSON request.
  void jsonizeRequest(const Http::RequestHeaderMap& headers, const Buffer::Instance* body,
                      Buffer::Instance& out) const;
  // Convert the JSON response to a standard HTTP response.
  void dejsonizeResponse(Http::ResponseHeaderMap& headers, const Buffer::Instance& body,
                         Buffer::Instance& out);
  const FilterSettings settings_;
  FilterStats stats_;
  Http::RequestHeaderMap* request_headers_ = nullptr;
  Http::ResponseHeaderMap* response_headers_ = nullptr;
  std::shared_ptr<Extensions::Common::Aws::Signer> sigv4_signer_;
  absl::optional<Arn> arn_;
  InvocationMode invocation_mode_ = InvocationMode::Synchronous;
  bool payload_passthrough_ = false;
  bool skip_ = false;
};

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
