1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

* sip-proxy: change API by replacing ``own_domain`` with :ref:`local_services <envoy_v3_api_msg_extensions.filters.network.sip_proxy.v3alpha.LocalService>`.
* tls: set TLS v1.2 as the default minimal version for servers. Users can still explicitly opt-in to 1.0 and 1.1 using :ref:`tls_minimum_protocol_version <envoy_v3_api_field_extensions.transport_sockets.tls.v3.TlsParameters.tls_minimum_protocol_version>`.
* tls-inspector: the listener filter tls inspector's stats ``connection_closed`` and ``read_error`` are removed. The new stats are introduced for listener, ``downstream_peek_remote_close`` and ``read_error`` :ref:`listener stats <config_listener_stats>`.

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

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
