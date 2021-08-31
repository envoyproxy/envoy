1.18.5 (Pending)
=====================

Incompatible Behavior Changes
-----------------------------

Minor Behavior Changes
----------------------

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* ext_authz: fix the ext_authz filter to correctly merge multiple same headers using the ',' as separator in the check request to the external authorization service.
* http: limit use of deferred resets in the http2 codec to server-side connections. Use of deferred reset for client connections can result in incorrect behavior and performance problems.
* jwt_authn: unauthorized responses now correctly include a `www-authenticate` header.
* listener: fixed an issue on Windows where connections are not handled by all worker threads.

Removed Config or Runtime
-------------------------

New Features
------------

Deprecated
----------
