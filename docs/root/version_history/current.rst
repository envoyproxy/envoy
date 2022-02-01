1.18.6 (Pending)
=====================

Incompatible Behavior Changes
-----------------------------

Minor Behavior Changes
----------------------

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* data plane: fix crash when internal redirect selects a route configured with direct response or redirect actions.
* jwt_authn: fixed the crash when a CONNECT request is sent to JWT filter configured with regex match on the Host header.

Removed Config or Runtime
-------------------------

New Features
------------

Deprecated
----------
