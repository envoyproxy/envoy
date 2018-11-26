.. _config_network_filters_mysql_proxy:

MySQL proxy
===========

The MySQL proxy filter decodes the wire protocol between the MySQL client
and server. It decodes the SQL queries in the payload (SQL99 format only).
The decoded info is emitted as dynamic metadata that can be combined with
access log filters to get detailed information on tables accessed as well
as operations performed on each table.

.. _config_network_filters_mysql_proxy_stats:

Statistics
----------

Every configured MySQL proxy filter has statistics rooted at *mysql.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  auth_switch_request, Counter, Number of times the upstream server requested clients to switch to a different authentication method
  login_attempts, Counter, Number of login attempts
  login_failures, Counter, Number of login failures
  protocol_errors, Counter, Number of out of sequence protocol messages encountered in a session
  sessions, Counter, Number of MySQL sessions since start
  upgraded_to_ssl, Counter, Number of sessions/connections that were upgraded to SSL

.. _config_network_filters_mysql_proxy_dynamic_metadata:

Dynamic Metadata
----------------

The MySQL filter emits the following dynamic metadata for each SQL query parsed:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  <table.db>, string, The resource name in *table.db* format. If the database cannot be inferred, the resource name contains the table being accessed.
  [], list, A list of strings, representing the operations executed on the resource. Operations can be one of insert,update,select,drop,delete,create,alter,show.
