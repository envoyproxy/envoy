1.19.4 (April 25, 2022)
=======================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* perf: ssl contexts are now tracked without scan based garbage collection and greatly improved the performance on secret update.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* docker: update Docker images to resolve CVE issues in container packages (#20760).

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
