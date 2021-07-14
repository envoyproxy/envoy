1.20.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

* listener: added the :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`
  field and changed the default for reuse port from false to true, as the feature is now well
  supported on the majority of production Linux kernels in use. The default change is aware of hot
  restart, as otherwise the change would not be backwards compatible between restarts. This means
  that hot restarting on to a new binary will retain the default of false until the binary undergoes
  a full restart. To retain the previous behavior, either explicitly set the new configuration
  field to false, or set the runtime feature flag `envoy.reloadable_features.listener_reuse_port_default_enabled`
  to false. As part of this change, the use of reuse port for TCP listeners on both OSX and
  Windows has been disabled due to suboptimal behavior. See the field documentation for more
  information.
* http: set the default :ref:`lazy headermap threshold <arch_overview_http_header_map_settings>` to 3,
  which defines the minimal number of headers in a request/response/trailers required for using a
  dictionary in addition to the list. Setting the `envoy.http.headermap.lazy_map_min_size` runtime
  feature to a non-negative number will override the default value.

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.http_upstream_wait_connect_response`` runtime guard and legacy code paths.
* http: removed ``envoy.reloadable_features.allow_preconnect`` runtime guard and legacy code paths.

New Features
------------

Deprecated
----------

* listener: :ref:`reuse_port <envoy_v3_api_field_config.listener.v3.Listener.reuse_port>` has been
  deprecated in favor of :ref:`enable_reuse_port <envoy_v3_api_field_config.listener.v3.Listener.enable_reuse_port>`.
  At the same time, the default has been changed from false to true. See above for more information.
