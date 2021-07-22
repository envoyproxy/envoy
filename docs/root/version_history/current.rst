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

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------
* listener: added an option when balancing across active listeners and wildcard matching is used to return the listener that matches the IP family type associated with the listener's socket address. It is off by default, but is turned on by default in v1.19. To set change the runtime guard `envoy.reloadable_features.listener_wildcard_match_ip_family` to true.

Deprecated
----------
