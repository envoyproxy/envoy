1.22.5 (August 12, 2022)
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

* listener: fixed a bug that doesn't handle of an update for a listener with IPv4-mapped address correctly, and that will lead to a memory leak.
* repo: fix version to resolve release issue.
* transport_socket: fixed a bug that prevented the tcp stats to be retrieved when running on kernels different than the kernel where Envoy was built.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`


New Features
------------

Deprecated
----------
