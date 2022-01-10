1.20.1 (November 30, 2021)
==========================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* config: the log message for "gRPC config stream closed" now uses the most recent error message, and reports seconds instead of milliseconds for how long the most recent status has been received.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* http: remove redundant Warn log in HTTP codec.
* listener: fix a crash when updating any listener that does not bind to port.
* listener: listener add can reuse the listener socket of a draining filter chain listener and fix the request lost.
* mac: fix crash on startup on macOS 12 by changing the default allocator.
* tcp: fixed a bug where upstream circuit breakers applied HTTP per-request bounds to TCP connections.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
