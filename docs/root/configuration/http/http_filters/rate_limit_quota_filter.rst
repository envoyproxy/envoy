.. _config_http_filters_rate_limit_quota:

Rate Limit Quota
================

TODO(yanvlasov): fix references after not-implemented-hide is removed from the proto file

* Global rate limiting +ref+`architecture overview <arch_overview_global_rate_limit>`
* +ref+`v3 API reference <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>`

This filter provides implementation of the global rate limit quota +ref+`protocol <envoy_v3_api_msg_service.rate_limit_quota.v3.RateLimitQuotaService>`.
The rate limit quota service (RLQS) provides quota assignments to each Envoy instance connected to the service. In addition to enforcing rate limit quota assignments,
this filter periodically reports request rates for each assignment to the RLQS, allowing it to rebalance quota assignments between Envoy instances depending on the
individual load of each Envoy instance. When quota assignments change the RLQS proactively pushes them to Envoy.

The HTTP rate limit quota filter will call the rate limit quota service when it is configured in the HTTP connection manager filter chain. Filter configuration
defines the RLQS service and definitions of request buckets that will receive quota assignments. Request buckets are defined by a set of matchers that determine
if a request is subject to the rate limit quota assigned to that bucket. Each matcher can contain multiple buckets by the means of the
+ref+`bucket_id_builder <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig.bucket_id_builder>`. The bucket ID builder allows
request buckets to be generated dynamically based on request attributes, such as request header value.

If a request does not match any set of matchers then quota assignment for the "catch all" bucket is applied (TODO(yanavlasov): how is the "catch all" bucket configured).

Bucket definitions can be overridden in the virtual host or route configurations. The more specific definition completely overrides the less specific definition.

Initially all Envoy's quota assignments are empty. The rate limit quota filter requests quota assignment from RLQS when the request matches a bucket for the first time.
The behavior of the filter while it waits for the initial assignment is determined by the `no_assignment_behavior` value. In this state requests can either all be
immediately allowed, denied or enqueued until quota assignment is received.

A quota assignment may have associated time to live +ref+. The RLQS is expected to update the assignment before TTL runs out. If RLQS failed to update the assignment and its TTL
has expired, the filter can be configured to continue using the last quota assignment or fall back to a value predefined in the configuration +ref+.

The rate limit quota filter reports the request load for each bucket to the RLQS with the configured `reporting_interval`. The RLQS may rebalance quota assignments based on the request
load that each Envoy receives and push new quota assignments to Envoys.

When connection to RLQS server fails the filter will fall back to either the
+ref+`no assignment behavior <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig.no_assignment_behavior>`
if it has not yet received rate limit quota or eventually to the
+ref+`expired assignment behavior <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig.expired_assignment_behavior>` if
connection could not be re-established by the time the existing quota expired.

Example 1
^^^^^^^^^

In this example HTTP connection manager has the following bucket definitions in the rate limit quota filter
+ref+`configuration <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>`. This
configuration enables rate limit quota filter with 3 buckets. Note that bucket ID is a map of key-value pairs.

1.  Bucket id ``name: prod-rate-limit-quota`` for all requests with the ``deployment: prod`` header present. Until RLQS assigns a quota
    all requests are allowed.

1.  Bucket id ``name: staging-rate-limit-quota`` for all requests with the ``deployment: staging`` header present. Until RLQS assigns a quota
    all requests are denied.

1.  Bucket id ``name: default-rate-limit-quota`` for all other requests. Until RLQS assigns a quota 1K RPS quota is applied.

.. code-block:: yaml

  rlqs_server:
    envoy_grpc:
      cluster_name: rate_limit_quota_service
  domain: "acme-services"
  matcher:
    matcher_list:
      matchers:
      - predicate:
        - single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: deployment
            value_match:
              exact: prod
        on_match:
          action:
            name: prod-bucket
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
              bucket_id_builder:
                bucket_id_builder:
                  "name":
                    string_value: "prod-rate-limit-quota"
              reporting_interval: 60s
              no_assignment_behavior:
                blanket_rule: ALLOW_ALL
      - predicate:
        - single_predicate:
            input:
              name: request-headers
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: deployment
            value_match:
              exact: staging
        on_match:
          action:
            name: staging-bucket
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
              bucket_id_builder:
                bucket_id_builder:
                  "name":
                    string_value: "staging-rate-limit-quota"
              reporting_interval: 60s
              no_assignment_behavior:
                blanket_rule: DENY_ALL
    # The "catch all" bucket settings
    on_no_match:
      action:
        name: default-bucket
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
          bucket_id_builder:
            bucket_id_builder:
              "name":
                string_value: "default-rate-limit-quota"
          reporting_interval: 60s
          deny_response_settings:
            http_status_code: 429
          no_assignment_behavior:
            blanket_rule: ALLOW_ALL
          expired_assignment_behavior:
            fallback_rate_limit:
              requests_per_time_unit:
                requests_per_time_unit: 1000
                time_unit: 1s


Rate Limit Quota Override
-------------------------

TODO

Matcher extensions
------------------

TODO

Statistics
----------

TODO

Dynamic Metadata
----------------

TODO

Runtime
-------

TODO
