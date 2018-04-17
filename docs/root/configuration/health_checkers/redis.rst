.. _config_health_checkers_redis:

Redis
=====

The Redis health checker is a custom health checker which checks Redis upstream hosts. It sends
a Redis PING command and expect a PONG response. The upstream Redis server can respond with
anything other than PONG to cause an immediate active health check failure. Optionally, Envoy can
perform EXISTS on a user-specified key. If the key does not exist it is considered a passing healthcheck.
This allows the user to mark a Redis instance for maintenance by setting the specified
:ref:`key <envoy_api_field_config.health_checker.redis.v2.Redis.key>` to any value and waiting for
traffic to drain.

* :ref:`v2 API reference <envoy_api_msg_core.HealthCheck.CustomHealthCheck>`