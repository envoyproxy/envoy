.. _config_rate_limit_service:

Rate limit service
==================

The :ref:`rate limit service <arch_overview_global_rate_limit>` configuration specifies the global rate
limit service Envoy should talk to when it needs to make global rate limit decisions. If no rate
limit service is configured, a "null" service will be used which will always return OK if called.

* :ref:`v3 API reference <envoy_v3_api_msg_config.ratelimit.v3.RateLimitServiceConfig>`

gRPC service IDL
----------------

Envoy expects the rate limit service to support the gRPC IDL specified in
:ref:`rls.proto <envoy_v3_api_file_envoy/service/ratelimit/v3/rls.proto>`. See the IDL documentation
for more information on how the API works. See Envoy's reference implementation
`here <https://github.com/envoyproxy/ratelimit>`_.

.. _config_rate_limit_quota_service:

Rate limit quota service
========================

Envoy uses global rate limit quota service when it needs to obtain rate limit quota assignments for incoming
requests. If the rate limit quota service is not available Envoy uses the
:ref:`no assignment behavior <envoy_v3_api_field_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings.no_assignment_behavior>`
configuration.


* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig>`


gRPC rate limit quota service IDL
---------------------------------

Envoy expects the rate limit quota service to support the gRPC IDL specified in
:ref:`rls.proto <envoy_v3_api_file_envoy/service/rate_limit_quota/v3/rlqs.proto>`. See the IDL documentation
for more information on how the API works.

Open source reference implementation of the rate limiting service is currently unavailable. The rate limit
quota extension can be presently used with the Google Cloud Rate Limit Service.
