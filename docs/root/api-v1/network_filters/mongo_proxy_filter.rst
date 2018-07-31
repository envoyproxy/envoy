.. _config_network_filters_mongo_proxy_v1:

Mongo proxy
===========

MongoDB :ref:`configuration overview <config_network_filters_mongo_proxy>`.

.. code-block:: json

  {
    "name": "mongo_proxy",
    "config": {
      "stat_prefix": "...",
      "access_log": "...",
      "fault": {}
    }
  }

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_mongo_proxy_stats>`.

access_log
  *(optional, string)* The optional path to use for writing Mongo access logs. If not access log
  path is specified no access logs will be written. Note that access log is also gated by
  :ref:`runtime <config_network_filters_mongo_proxy_runtime>`.

fault
  *(optional, object)* If specified, the filter will inject faults based on the values in the object.

Fault configuration
-------------------

Configuration for MongoDB fixed duration delays. Delays are applied to the following MongoDB
operations: Query, Insert, GetMore, and KillCursors. Once an active delay is in progress, all
incoming data up until the timer event fires will be a part of the delay.

.. code-block:: json

  {
    "fixed_delay": {
      "percent": "...",
      "duration_ms": "..."
    }
  }

percent
  *(required, integer)* Probability of an eligible MongoDB operation to be affected by the
  injected fault when there is no active fault. Valid values are integers in a range of [0, 100].

duration_ms
  *(required, integer)* Non-negative delay duration in milliseconds.

