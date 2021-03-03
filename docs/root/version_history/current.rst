1.15.4 (Pending)
================

Changes
-------

* http: fixed URL parsing for HTTP/1.1 fully qualified URLs and connect requests containing IPv6 addresses.
* http: fixed bugs in datadog and squash filter's handling of responses with no bodies.
* http: reverting a behavioral change where upstream connect timeouts were temporarily treated differently from other connection failures. The change back to the original behavior can be temporarily reverted by setting `envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure` to false.
* tls: fix detection of the upstream connection close event.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

New Features
------------

Deprecated
----------
