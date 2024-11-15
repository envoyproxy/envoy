.. _config_health_checkers_redis:

Redis
=====

The Redis health checker is a custom health checker (with :code:`envoy.health_checkers.redis` as name)
which checks Redis upstream hosts. It sends a Redis PING command and expect a PONG response. The upstream
Redis server can respond with anything other than PONG to cause an immediate active health check failure.
Optionally, Envoy can perform EXISTS on a user-specified key. If the key does not exist it is considered a
passing health check. This allows the user to mark a Redis instance for maintenance by setting the
specified :ref:`key <envoy_v3_api_field_extensions.health_checkers.redis.v3.Redis.key>` to any value and waiting
for traffic to drain.

An example setting for :ref:`custom_health_check <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>` as a
Redis health checker is shown below:

.. code-block:: yaml

  custom_health_check:
    name: envoy.health_checkers.redis
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.redis.v3.Redis
        key: foo

* :ref:`v3 API reference <envoy_v3_api_msg_config.core.v3.HealthCheck.CustomHealthCheck>`

Statistics
----------

The Redis health checker emits some statistics in the *health_check.redis.* namespace.

.. csv-table::
     :header: Name, Description
     :widths: 1, 2

     exists_failure, Total number of health check failures caused by EXISTS check failure.
