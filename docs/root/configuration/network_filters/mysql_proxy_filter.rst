.. _config_network_filters_mysql_proxy:

Mysql proxy
===========

- :ref:`v2 API reference <envoy_api_msg_config.filter.network.mysql_proxy.v2.MysqlProxy>`

.. _config_network_filters_mysql_proxy_stats:

Statistics
----------

Every configured Mysql proxy filter has statistics rooted at *mysql.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  new_sessions, Counter, Number of Mysql sessions since start
  total_mysql_headers, Counter, Number of times the delay is injected
  byte_count, Counter, Number of OP_GET_MORE messages
  login_attempts, Counter, Number of OP_INSERT messages
  login_failures, Counter, Number of OP_KILL_CURSORS messages
  total_queries, Counter, Number of OP_QUERY messages
  query_failures, Counter, Number of OP_QUERY with tailable cursor flag set
  wrong_sequence, Counter, Number of OP_QUERY with no cursor timeout flag set
  ssl_pass_through, Counter, Number of OP_QUERY with await data flag set
  auth_switch_request, Counter, Number of OP_QUERY with exhaust flag set

