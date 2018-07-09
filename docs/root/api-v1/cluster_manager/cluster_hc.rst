.. _config_cluster_manager_cluster_hc_v1:

Health checking
===============

* Health checking :ref:`architecture overview <arch_overview_health_checking>`.
* If health checking is configured for a cluster, additional statistics are emitted. They are
  documented :ref:`here <config_cluster_manager_cluster_stats>`.

.. code-block:: json

  {
    "type": "...",
    "timeout_ms": "...",
    "interval_ms": "...",
    "unhealthy_threshold": "...",
    "healthy_threshold": "...",
    "path": "...",
    "send": [],
    "receive": [],
    "interval_jitter_ms": "...",
    "service_name": "...",
    "redis_key": "..."
  }

type
  *(required, string)* The type of health checking to perform. Currently supported types are
  *http*, *redis*, and *tcp*. See the :ref:`architecture overview <arch_overview_health_checking>`
  for more information.

timeout_ms
  *(required, integer)* The time in milliseconds to wait for a health check response. If the
  timeout is reached the health check attempt will be considered a failure.

.. _config_cluster_manager_cluster_hc_interval:

interval_ms
  *(required, integer)* The interval between health checks in milliseconds.

unhealthy_threshold
  *(required, integer)* The number of unhealthy health checks required before a host is marked
  unhealthy. Note that for *http* health checking if a host responds with 503 this threshold is
  ignored and the host is considered unhealthy immediately.

healthy_threshold
  *(required, integer)* The number of healthy health checks required before a host is marked
  healthy. Note that during startup, only a single successful health check is required to mark
  a host healthy.

path
  *(sometimes required, string)* This parameter is required if the type is *http*. It specifies the
  HTTP path that will be requested during health checking. For example */healthcheck*.

send
  *(sometimes required, array)* This parameter is required if the type is *tcp*. It specifies
  the bytes to send for a health check request. It is an array of hex byte strings specified
  as in the following example:

  .. code-block:: json

    [
      {"binary": "01"},
      {"binary": "000000FF"}
    ]

  The array is allowed to be empty in the case of "connect only" health checking.

receive
  *(sometimes required, array)* This parameter is required if the type is *tcp*. It specified the
  bytes that are expected in a successful health check response. It is an array of hex byte strings
  specified similarly to the *send* parameter. The array is allowed to be empty in the case of
  "connect only" health checking.

interval_jitter_ms
  *(optional, integer)* An optional jitter amount in millseconds. If specified, during every
  internal Envoy will add 0 to *interval_jitter_ms* milliseconds to the wait time.

.. _config_cluster_manager_cluster_hc_service_name:

service_name
  *(optional, string)* An optional service name parameter which is used to validate the identity of
  the health checked cluster. See the :ref:`architecture overview
  <arch_overview_health_checking_identity>` for more information.

.. _config_cluster_manager_cluster_hc_redis_key:

redis_key
  *(optional, string)* If the type is *redis*, perform ``EXISTS <redis_key>`` instead of
  ``PING``. A return value from Redis of 0 (does not exist) is considered a passing healthcheck. A
  return value other than 0 is considered a failure. This allows the user to mark a Redis instance
  for maintenance by setting the specified key to any value and waiting for traffic to drain.
