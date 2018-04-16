.. _config_cluster_manager_cluster_hc:

Health checking
===============

* Health checking :ref:`architecture overview <arch_overview_health_checking>`.
* If health checking is configured for a cluster, additional statistics are emitted. They are
  documented :ref:`here <config_cluster_manager_cluster_stats>`.
* :ref:`v1 API documentation <config_cluster_manager_cluster_hc_v1>`.
* :ref:`v2 API documentation <envoy_api_msg_core.HealthCheck>`.

.. _config_cluster_manager_cluster_hc_tcp_health_checking:

TCP health checking
-------------------

.. attention::

  This section is written for the v1 API but the concepts also apply to the v2 API. It will be
  rewritten to target the v2 API in a future release.

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
and in the order specified, but not necessarily contiguous. Thus, in the example above,
"FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
would still pass. This is done to support protocols that insert non-deterministic data, such as
time, into the response.

Health checks that require a more complex pattern such as send/receive/send/receive are not
currently possible.

If "receive" is an empty array, Envoy will perform "connect only" TCP health checking. During each
cycle, Envoy will attempt to connect to the upstream host, and consider it a success if the
connection succeeds. A new connection is created for each health check cycle.
