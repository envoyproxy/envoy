.. _config_network_filters_postgres_proxy:

Postgres proxy
================

The Postgres proxy filter decodes the wire protocol between Postgres client
and server. The decoded info is currently used only to produce statistics.

When the Postgres filter detects that a session is encrypted, the messages
are ignored and no decoding takes place.


.. attention::

   The `postgres_proxy` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Configuration
-------------

The Postgres proxy filter should be chained with the TCP proxy as shown in the configuration
example below:

.. code-block:: yaml

    filter_chains:
    - filters:
      - name: envoy.filters.network.postgres_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.postgres_proxy.v2alpha.PostgresProxy
          stat_prefix: postgres
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp
          cluster: postgres_cluster

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`

.. _config_network_filters_postgres_proxy_stats:

Statistics
----------

Every configured Postgres proxy filter has statistics rooted at postgres.<stat_prefix> with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  backend_msgs, Counter, Number of backend messages detected by the filter
  errors, Counter, Number of times the server replied with error
  frontend_msgs, Counter, Number of frontend messages detected by the filter
  sessions, Counter, Number of successful logins
  sessions_encrypted, Counter, Number of times the proxy detected encrypted sessions
  sessions_unencrypted, Counter, Number of messages indicating unencrypted successful login
  statements, Counter, Number of SQL statements
  statements_delete, Counter, Number of DELETE statements
  statements_insert, Counter, Number of INSERT statements
  statements_select, Counter, Number of SELECT statements
  statements_update, Counter, Number of UPDATE statements
  statements_other, Counter, "Number of statements other than DELETE, INSERT, SELECT or UPDATE"
  transactions, Counter, Number of SQL transactions
  transactions_commit, Counter, Number of COMMIT transactions
  transactions_rollback, Counter, Number of ROLLBACK transactions
  warnings, Counter, Number of time the server replied with warning
  unknown, Counter, Number of times the proxy successfully decoded a message but did not know what to do with it
