1.20.1 (Pending)
========================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* http: remove redundant Warn log in HTTP codec.
* listener: fix a crash when updating any listener that does not bind to port.
* listener: listener add can reuse the listener socket of a draining filter chain listener and fix the request lost.
* macOS: fix crash on startup on macOS 12 by changing the default allocator.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
