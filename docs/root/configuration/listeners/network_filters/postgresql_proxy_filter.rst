.. _config_network_filters_postgresql_proxy:

PostgreSQL proxy
================

The PostgreSQL proxy filter decodes the wire protocol between PostgreSQL client
and server. The decoded info is currently used only to produce statistics.


.. attention::

   The `postgresql_proxy` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Configuration
-------------

The PostgreSQL proxy filter should be chained with the TCP proxy as shown in the configuration
example below:

.. code-block:: yaml

    filter_chains:
    - filters:
      - name: envoy.filters.network.postgresql_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.postgresql_proxy.v2alpha.PostgreSQLProxy
          stat_prefix: postgresql
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          stat_prefix: tcp
          cluster: postgresql_cluster

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`

.. _config_network_filters_postgresql_proxy_stats:

Statistics
----------

Every configured PostgreSQL proxy filter has statistics rooted at postgresql.<stat_prefix> with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  errors, Counter, Number of times the server replied with error
  frontend_commands, Counter, Number of frontend commands detected by the filter
  statements, Counter, Number of SQL statements
  statements_delete, Counter, NUmber of DELETE statements
  statements_insert, Counter, NUmber of INSERT statements
  statements_select, Counter, Number of SELECT statements
  statements_update, Counter, NUmber of UPDATE statements
  statements_other, Counter, "Number of statements other than DELETE, INSERT, SELECT or UPDATE"
  transactions, Counter, Number of transactions
  transactions_commit, Counter, Number of COMMIT transactions
  transactions_rollback, Counter, Number of ROLLBACK transactions_rollback
  warnings, Counter, Number of time the server replied with warning
  unrecognized, Counter, Number of times the proxy successfully decoded a message but did not know what to do with it.
