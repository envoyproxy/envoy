1.16.1 (November 20, 2020)
==========================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*
* examples: examples use v3 configs.
* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* listener: fix crash when disabling or re-enabling listeners due to overload while processing LDS updates.
* proxy_proto: fixed a bug where the wrong downstream address got sent to upstream connections.
* proxy_proto: fixed a bug where network filters would not have the correct downstreamRemoteAddress() when accessed from the StreamInfo. This could result in incorrect enforcement of RBAC rules in the RBAC network filter (but not in the RBAC HTTP filter), or incorrect access log addresses from tcp_proxy.
* tls: fix read resumption after triggering buffer high-watermark and all remaining request/response bytes are stored in the SSL connection's internal buffers.
* udp: fixed issue in which receiving truncated UDP datagrams would cause Envoy to crash.
* vrp: allow supervisord to open its log file.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
