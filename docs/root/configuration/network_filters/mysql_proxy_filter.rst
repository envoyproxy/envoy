.. _config_network_filters_mysql_proxy:

Mysql proxy
===========

The Mysql proxy filter decodes the wire protocol between the Mysql client
and server. It does not decode the SQL queries in the payload, as of today.

.. _config_network_filters_mysql_proxy_stats:

Statistics
----------

Every configured Mysql proxy filter has statistics rooted at *mysql.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  sessions, Counter, Number of Mysql sessions since start
  login_attempts, Counter, Number of login attempts
  login_failures, Counter, Number of login failures
  protocol_errors, Counter, Number of out of sequence protocol messages encountered in a session
  upgraded_to_ssl, Counter, Number of sessions/connections that were upgraded to SSL
  auth_switch_request, Counter, Number of times the upstream server requested clients to switch to a different authentication method

