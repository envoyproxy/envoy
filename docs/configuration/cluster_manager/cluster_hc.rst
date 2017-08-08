.. _config_cluster_manager_cluster_hc:

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
    "service_name": "..."
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
  *(sometimes required, string)* This parameter is required if the type is *http*. It species the
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

.. _config_cluster_manager_cluster_hc_tcp_health_checking:

TCP health checking
-------------------

The type of matching performed is the following (this is the MongoDB health check request and
response):

.. code-block:: json

  {
    "send": [
      {"binary": "39000000"},
      {"binary": "EEEEEEEE"},
      {"binary": "00000000"},
      {"binary": "d4070000"},
      {"binary": "00000000"},
      {"binary": "746573742e"},
      {"binary": "24636d6400"},
      {"binary": "00000000"},
      {"binary": "FFFFFFFF"},
      {"binary": "13000000"},
      {"binary": "01"},
      {"binary": "70696e6700"},
      {"binary": "000000000000f03f"},
      {"binary": "00"}
     ],
     "receive": [
      {"binary": "EEEEEEEE"},
      {"binary": "01000000"},
      {"binary": "00000000"},
      {"binary": "0000000000000000"},
      {"binary": "00000000"},
      {"binary": "11000000"},
      {"binary": "01"},
      {"binary": "6f6b"},
      {"binary": "00000000000000f03f"},
      {"binary": "00"}
     ]
 }

During each health check cycle, all of the "send" bytes are sent to the target server. Each
binary block can be of arbitrary length and is just concatenated together when sent. (Separating
into multiple blocks can be useful for readability).

When checking the response, "fuzzy" matching is performed such that each binary block must be found,
and in the order specified, but not necessarly contiguous. Thus, in the example above,
"FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
would still pass. This is done to support protocols that insert non-deterministic data, such as
time, into the response.

Health checks that require a more complex pattern such as send/receive/send/receive are not
currently possible.

If "receive" is an empty array, Envoy will perform "connect only" TCP health checking. During each
cycle, Envoy will attempt to connect to the upstream host, and consider it a success if the
connection succeeds. A new connection is created for each health check cycle.
