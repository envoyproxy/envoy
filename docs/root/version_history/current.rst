1.16.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* http: the per-stream FilterState maintained by the HTTP connection manager will now provide read/write access to the downstream connection FilterState. As such, code that relies on interacting with this might
  see a change in behavior.
* router: now consumes all retry related headers to prevent them from being propagated to the upstream. This behavior may be reverted by setting runtime feature `envoy.reloadable_features.consume_all_retry_headers` to false.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed legacy header sanitization and the runtime guard `envoy.reloadable_features.strict_header_validation`.
* http: removed legacy transfer-encoding enforcement and runtime guard `envoy.reloadable_features.reject_unsupported_transfer_encodings`.

New Features
------------
* tap: added :ref:`generic body matcher<envoy_v3_api_msg_config.tap.v3.HttpGenericBodyMatch>` to scan http requests and responses for text or hex patterns.

Deprecated
----------

