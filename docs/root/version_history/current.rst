1.18.0 (Pending)
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

* active http health checks: properly handles HTTP/2 GOAWAY frames from the upstream. Previously a GOAWAY frame due to a graceful listener drain could cause improper failed health checks due to streams being refused by the upstream on a connection that is going away. To revert to old GOAWAY handling behavior, set the runtime feature `envoy.reloadable_features.health_check.graceful_goaway_handling` to false.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* tcp_proxy: add support for converting raw TCP streams into HTTP/1.1 CONNECT requests. See :ref:`upgrade documentation <tunneling-tcp-over-http>` for details.

Deprecated
----------
