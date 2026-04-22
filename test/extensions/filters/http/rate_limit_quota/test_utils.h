#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

inline constexpr absl::string_view ValidMatcherConfig = R"EOF(
  matcher_list:
    matchers:
      # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
            name: "HttpRequestHeaderMatchInput"
          value_match:
            exact: staging
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "name":
                    string_value: "prod"
                "environment":
                    custom_value:
                      name: "test_1"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: environment
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: group
            reporting_interval: 60s
  )EOF";

inline constexpr absl::string_view ValidPreviewMatcherConfig = R"EOF(
  matcher_list:
    matchers:
      # Assign requests with header['env'] set to 'staging' to the bucket { name: 'staging' }
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
            name: "HttpRequestHeaderMatchInput"
          value_match:
            exact: staging
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "name":
                    string_value: "prod"
                "environment":
                    custom_value:
                      name: "test_1"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: environment
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        header_name: group
                "preview_name":
                    string_value: "preview_test"
            reporting_interval: 60s
        keep_matching: true
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "name":
                string_value: "prod"
            "environment":
                custom_value:
                  name: "test_1"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: environment
            "group":
                custom_value:
                  name: "test_2"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: group
        no_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: DENY_ALL
        reporting_interval: 5s
  )EOF";

inline constexpr absl::string_view InvalidMatcherConfig = R"EOF(
  matcher_list:
    matchers:
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "group":
                    custom_value:
                      name: "test_2"
                      typed_config:
                        "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                        # No value is defined here, it will cause the failure of generation of bucket id.
                        header_name:
            reporting_interval: 60s
  )EOF";

inline constexpr absl::string_view OnNoMatchConfig = R"EOF(
  matcher_list:
    matchers:
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                # The on_match field here will not be matched by the request header.
                "NO_MATCHED_NAME":
                    string_value: "NO_MATCHED"
            reporting_interval: 60s
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "on_no_match_name":
                string_value: "on_no_match_value"
            "on_no_match_name_2":
                string_value: "on_no_match_value_2"
        deny_response_settings:
          grpc_status:
            code: 8
          http_status:
            code: 403
          http_body:
            value: "Test-rejection-body"
          response_headers_to_add:
            header:
              key: "test-onnomatch-header"
              value: "test-onnomatch-value"
        no_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: DENY_ALL
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

// No matcher type (matcher_list or matcher_tree) is configure here. It will read from `on_no_match`
// field directly.
inline constexpr absl::string_view OnNoMatchConfigWithNoMatcher = R"EOF(
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "on_no_match_name":
                string_value: "on_no_match_value"
            "on_no_match_name_2":
                string_value: "on_no_match_value_2"
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

// By design, on_no_match here only supports static bucket_id generation (via string value) and
// doesn't support dynamic way (via custom_value extension). So the configuration with
// `custom_value` typed_config is invalid configuration that will cause the failure of generating
// the bucket id.
inline constexpr absl::string_view InvalidOnNoMatcherConfig = R"EOF(
  matcher_list:
    matchers:
      predicate:
        single_predicate:
          input:
            typed_config:
              "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
              header_name: environment
          value_match:
            exact: staging
      # Here is on_match field that will not be matched by the request header.
      on_match:
        action:
          name: rate_limit_quota
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
            bucket_id_builder:
              bucket_id_builder:
                "NO_MATCHED_NAME":
                    string_value: "NO_MATCHED"
            reporting_interval: 60s
  on_no_match:
    action:
      name: rate_limit_quota
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
        bucket_id_builder:
          bucket_id_builder:
            "environment":
                custom_value:
                  name: "test_1"
                  typed_config:
                    "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                    header_name: environment
        deny_response_settings:
          grpc_status:
            code: 8
        expired_assignment_behavior:
          fallback_rate_limit:
            blanket_rule: ALLOW_ALL
        reporting_interval: 5s
)EOF";

inline constexpr absl::string_view GoogleGrpcConfig = R"EOF(
  rlqs_server:
    google_grpc:
      target_uri: rate_limit_quota_server
      stat_prefix: google
  )EOF";

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
