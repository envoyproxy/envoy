1.22.11 (April 11, 2023)
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

* dependency: update Curl -> 8.0.1 to resolve CVE-2023-27535, CVE-2023-27536, CVE-2023-27538.
* http: amend the fix for ``x-envoy-original-path`` so it removes the header only at edge.
  Previously this would also remove the header at any Envoy instance upstream of an external request, including an Envoy instance that added the header.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
