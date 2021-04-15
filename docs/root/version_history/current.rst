1.18.0 (April 15, 2021)
=======================

Bug Fixes
---------

code: fixed some whitespace to make fix_format happy.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* tls: removed `envoy.reloadable_features.tls_use_io_handle_bio` runtime guard and legacy code path.
