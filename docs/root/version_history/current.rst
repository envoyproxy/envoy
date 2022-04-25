1.23.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* local_ratelimit: local_ratelimit will consume tokens of all matched descriptors. This behavioral change can be reverted by setting runtime guard ``envoy.reloadable_features.http_local_ratelimit_match_all_descriptors`` to false.
* tls: removed SHA-1 cipher suites from the server-side defaults.

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
