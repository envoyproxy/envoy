.. _config_rate_limit_service:

Rate limit service
==================

The :ref:`rate limit service <arch_overview_rate_limit>` configuration specifies the global rate
limit service Envoy should talk to when it needs to make global rate limit decisions. If no rate
limit service is configured, a "null" service will be used which will always return OK if called.

* :ref:`v2 API reference <envoy_api_msg_config.ratelimit.v2.RateLimitServiceConfig>`

gRPC service IDL
----------------

Envoy expects the rate limit service to support the gRPC IDL specified in
:ref:`rls.proto <envoy_api_file_envoy/service/ratelimit/v2/rls.proto>`. See the IDL documentation
for more information on how the API works. See Lyft's reference implementation
`here <https://github.com/lyft/ratelimit>`_.
