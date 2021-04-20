1.19.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* http: replaced setting `envoy.reloadable_features.strict_1xx_and_204_response_headers` with settings
  `envoy.reloadable_features.require_strict_1xx_and_204_response_headers`
  (require upstream 1xx or 204 responses to not have Transfer-Encoding or non-zero Content-Length headers) and
  `envoy.reloadable_features.send_strict_1xx_and_204_response_headers`
  (do not send 1xx or 204 responses with these headers). Both are true by default.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* zipkin: fix timestamp serializaiton in annotations. A prior bug fix exposed an issue with timestamps being serialized as strings.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* tls: removed `envoy.reloadable_features.tls_use_io_handle_bio` runtime guard and legacy code path.

New Features
------------

Deprecated
----------
