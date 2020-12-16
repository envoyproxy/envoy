1.16.3 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* lua: fixed crash when Lua script contains streamInfo():downstreamSslConnection().
* dns: fix a bug where custom resolvers provided in configuration were not preserved after network issues.
* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: sending CONNECT_ERROR for HTTP/2 where appropriate during CONNECT requests.
* proxy_proto: fixed a bug where the wrong downstream address got sent to upstream connections.
* tls: fix detection of the upstream connection close event.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
