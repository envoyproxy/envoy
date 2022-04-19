1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* tls-inspector: the listener filter tls inspector's stats ``connection_closed`` and ``read_error`` are removed. The new stats are introduced for listener, ``downstream_peek_remote_close`` and ``read_error`` :ref:`listener stats <config_listener_stats>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* tls: removed SHA-1 cipher suites from the server-side defaults.
* thrift: add validate_clusters in :ref:`RouteConfiguration <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.RouteConfiguration>` to override the default behavior of cluster validation.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
