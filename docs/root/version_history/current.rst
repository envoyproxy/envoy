1.17.4 (Pending)
=======================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* http: fixed an issue where Enovy did not handle peer stream limits correctly, and queued streams in nghttp2 rather than establish new connections. This behavior can be temporarily reverted by setting `envoy.reloadable_features.improved_stream_limit_handling` to false.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
