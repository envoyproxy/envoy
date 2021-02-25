1.17.1 (Pending)
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

* overload: fix a bug that can cause use-after-free when one scaled timer disables another one with the same duration.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* dispatcher: supports a stack of `Envoy::ScopeTrackedObject` instead of a single tracked object. This will allow Envoy to dump more debug information on crash.

Deprecated
----------
