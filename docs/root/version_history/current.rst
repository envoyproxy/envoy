1.17.2 (Pending)
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

* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
