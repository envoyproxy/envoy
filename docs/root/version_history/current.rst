1.18.2 (April 15, 2021)
=======================

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

code: fixed more build issues on our path to a glorious release.
