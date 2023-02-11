1.22.8 (February 24, 2023)
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

* dependency: update Kafka to resolve CVE-2023-25194.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

* docker: unify published images as tag variants. For example, ``envoyproxy/envoy-dev`` is now available
  as ``envoyproxy/envoy:dev``.

Deprecated
----------
