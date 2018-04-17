.. _config_network_filters_mongo_proxy:

Mongo proxy
===========

- MongoDB :ref:`architecture overview <arch_overview_mongo>`
- :ref:`v1 API reference <config_network_filters_mongo_proxy_v1>`
- :ref:`v2 API reference <envoy_api_msg_config.filter.network.mongo_proxy.v2.MongoProxy>`

.. _config_network_filters_mongo_proxy_fault_injection:

Fault injection
---------------

The Mongo proxy filter supports fault injection. See the v1 and v2 API reference for how to
configure.

.. _config_network_filters_mongo_proxy_stats:

Statistics
----------

Every configured MongoDB proxy filter has statistics rooted at *mongo.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decoding_error, Counter, Number of MongoDB protocol decoding errors
  delay_injected, Counter, Number of times the delay is injected
  op_get_more, Counter, Number of OP_GET_MORE messages
  op_insert, Counter, Number of OP_INSERT messages
  op_kill_cursors, Counter, Number of OP_KILL_CURSORS messages
  op_query, Counter, Number of OP_QUERY messages
  op_query_tailable_cursor, Counter, Number of OP_QUERY with tailable cursor flag set
  op_query_no_cursor_timeout, Counter, Number of OP_QUERY with no cursor timeout flag set
  op_query_await_data, Counter, Number of OP_QUERY with await data flag set
  op_query_exhaust, Counter, Number of OP_QUERY with exhaust flag set
  op_query_no_max_time, Counter, Number of queries without maxTimeMS set
  op_query_scatter_get, Counter, Number of scatter get queries
  op_query_multi_get, Counter, Number of multi get queries
  op_query_active, Gauge, Number of active queries
  op_reply, Counter, Number of OP_REPLY messages
  op_reply_cursor_not_found, Counter, Number of OP_REPLY with cursor not found flag set
  op_reply_query_failure, Counter, Number of OP_REPLY with query failure flag set
  op_reply_valid_cursor, Counter, Number of OP_REPLY with a valid cursor
  cx_destroy_local_with_active_rq, Counter, Connections destroyed locally with an active query
  cx_destroy_remote_with_active_rq, Counter, Connections destroyed remotely with an active query
  cx_drain_close, Counter, Connections gracefully closed on reply boundaries during server drain

Scatter gets
^^^^^^^^^^^^

Envoy defines a *scatter get* as any query that does not use an *_id* field as a query parameter.
Envoy looks in both the top level document as well as within a *$query* field for *_id*.

Multi gets
^^^^^^^^^^

Envoy defines a *multi get* as any query that does use an *_id* field as a query parameter, but
where *_id* is not a scalar value (i.e., a document or an array). Envoy looks in both the top level
document as well as within a *$query* field for *_id*.

.. _config_network_filters_mongo_proxy_comment_parsing:

$comment parsing
^^^^^^^^^^^^^^^^

If a query has a top level *$comment* field (typically in addition to a *$query* field), Envoy will
parse it as JSON and look for the following structure:

.. code-block:: json

  {
    "callingFunction": "..."
  }

callingFunction
  *(required, string)* the function that made the query. If available, the function will be used
  in :ref:`callsite <config_network_filters_mongo_proxy_callsite_stats>` query statistics.

Per command statistics
^^^^^^^^^^^^^^^^^^^^^^

The MongoDB filter will gather statistics for commands in the *mongo.<stat_prefix>.cmd.<cmd>.*
namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Number of commands
  reply_num_docs, Histogram, Number of documents in reply
  reply_size, Histogram, Size of the reply in bytes
  reply_time_ms, Histogram, Command time in milliseconds

.. _config_network_filters_mongo_proxy_collection_stats:

Per collection query statistics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The MongoDB filter will gather statistics for queries in the
*mongo.<stat_prefix>.collection.<collection>.query.* namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  total, Counter, Number of queries
  scatter_get, Counter, Number of scatter gets
  multi_get, Counter, Number of multi gets
  reply_num_docs, Histogram, Number of documents in reply
  reply_size, Histogram, Size of the reply in bytes
  reply_time_ms, Histogram, Query time in milliseconds

.. _config_network_filters_mongo_proxy_callsite_stats:

Per collection and callsite query statistics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the application provides the :ref:`calling function
<config_network_filters_mongo_proxy_comment_parsing>` in the *$comment* field, Envoy will generate
per callsite statistics. These statistics match the :ref:`per collection statistics
<config_network_filters_mongo_proxy_collection_stats>` but are found in the
*mongo.<stat_prefix>.collection.<collection>.callsite.<callsite>.query.* namespace.

.. _config_network_filters_mongo_proxy_runtime:

Runtime
-------

The Mongo proxy filter supports the following runtime settings:

mongo.connection_logging_enabled
  % of connections that will have logging enabled. Defaults to 100. This allows only a % of
  connections to have logging, but for all messages on those connections to be logged.

mongo.proxy_enabled
  % of connections that will have the proxy enabled at all. Defaults to 100.

mongo.logging_enabled
  % of messages that will be logged. Defaults to 100. If less than 100, queries may be logged
  without replies, etc.

mongo.mongo.drain_close_enabled
  % of connections that will be drain closed if the server is draining and would otherwise
  attempt a drain close. Defaults to 100.

mongo.fault.fixed_delay.percent
  Probability of an eligible MongoDB operation to be affected by
  the injected fault when there is no active fault.
  Defaults to the *percent* specified in the config.

mongo.fault.fixed_delay.duration_ms
  The delay duration in milliseconds. Defaults to the *duration_ms* specified in the config.

Access log format
-----------------

The access log format is not customizable and has the following layout:

.. code-block:: json

  {"time": "...", "message": "...", "upstream_host": "..."}

time
  System time that complete message was parsed, including milliseconds.

message
  Textual expansion of the message. Whether the message is fully expanded depends on the context.
  Sometimes summary data is presented to avoid extremely large log sizes.

upstream_host
  The upstream host that the connection is proxying to, if available. This is populated if the
  filter is used along with the :ref:`TCP proxy filter <config_network_filters_tcp_proxy>`.
