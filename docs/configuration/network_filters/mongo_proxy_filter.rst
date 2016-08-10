.. _config_network_filters_mongo_proxy:

Mongo proxy
===========

MongoDB :ref:`architecture overview <arch_overview_mongo>`.

.. code-block:: json

  {
    "type": "both",
    "name": "mongo_proxy",
    "config": {
      "stat_prefix": "...",
      "access_log": "..."
    }
  }

stat_prefix
  *(required, string)* The prefix to use when emitting :ref:`statistics
  <config_network_filters_mongo_proxy_stats>`.

access_log
  *(optional, string)* The optional path to use for writing Mongo access logs. If not access log
  path is specified no access logs will be written. Note that access log is also gated by
  :ref:`runtime <config_network_filters_mongo_proxy_runtime>`.

.. _config_network_filters_mongo_proxy_stats:

Statistics
----------

Every configured MongoDB proxy filter has statistics rooted at *mongo.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decoding_error, Counter, Description
  op_get_more, Counter, Description
  op_insert, Counter, Description
  op_kill_cursors, Counter, Description
  op_query, Counter, Description
  op_query_tailable_cursor, Counter, Description
  op_query_no_cursor_timeout, Counter, Description
  op_query_await_data, Counter, Description
  op_query_exhaust, Counter, Description
  op_query_scatter_get, Counter, Description
  op_query_multi_get, Counter, Description
  op_query_active, Gauge, Description
  op_reply, Counter, Description
  op_reply_cursor_not_found, Counter, Description
  op_reply_query_failure, Counter, Description
  op_reply_valid_cursor, Counter, Description
  cx_destroy_local_with_active_rq, Counter, Description
  cx_destroy_remote_with_active_rq, Counter, Description

.. _config_network_filters_mongo_proxy_runtime:

Per query statistics
^^^^^^^^^^^^^^^^^^^^

FIXFIX

Per reply statistics
^^^^^^^^^^^^^^^^^^^^

FIXFIX

Runtime
-------

The Mongo proxy filter supports the following runtime settings:

mongo.connection_logging_enabled
  FIXFIX

mongo.proxy_enabled
  FIXFIX

mongo.logging_enabled
  FIXFIX

Access log format
-----------------

FIXFIX
