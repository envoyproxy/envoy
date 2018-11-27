.. _config_network_filters_mysql_proxy:

MySQL proxy
===========

The MySQL proxy filter decodes the wire protocol between the MySQL client
and server. It does not decode the SQL queries in the payload, as of today.

.. _config_network_filters_mysql_proxy_stats:

Statistics
----------

Every configured MySQL proxy filter has statistics rooted at *mysql.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  auth_switch_request, Counter, Number of times the upstream server requested clients to switch to a different authentication method
  decoder_errors, Counter, Number of MySQL protocol decoding errors
  login_attempts, Counter, Number of login attempts
  login_failures, Counter, Number of login failures
  protocol_errors, Counter, Number of out of sequence protocol messages encountered in a session
  sessions, Counter, Number of MySQL sessions since start
  upgraded_to_ssl, Counter, Number of sessions/connections that were upgraded to SSL
