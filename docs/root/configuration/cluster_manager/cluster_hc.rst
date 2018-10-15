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

The type of matching performed is the following:

.. code-block:: json

  {
    "send": [
      {"text": "39000000"},
      {"text": "EEEEEEEE"},
     ],
     "receive": [
      {"text": "EEEEEEEE"},
      {"text": "01000000"},
     ]
 }

During each health check cycle, all of the "send" bytes are sent to the target server. Each
hex encoded text block can be of arbitrary length and is just concatenated together when sent.
(Separating into multiple blocks can be useful for readability).

When checking the response, "fuzzy" matching is performed such that each block must be found,
and in the order specified, but not necessarily contiguous. Thus, in the example above,
"FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
would still pass. This is done to support protocols that insert non-deterministic data, such as
time, into the response.

Health checks that require a more complex pattern such as send/receive/send/receive are not
currently possible.

If "receive" is an empty array, Envoy will perform "connect only" TCP health checking. During each
cycle, Envoy will attempt to connect to the upstream host, and consider it a success if the
connection succeeds. A new connection is created for each health check cycle.
