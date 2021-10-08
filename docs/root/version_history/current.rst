1.21.0 (Pending)
================

Incompatible Behavior Changes
-----------------------------
*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*

Minor Behavior Changes
----------------------
*Changes that may cause incompatibilities for some users, but should not for most*

Bug Fixes
---------
*Changes expected to improve the state of the world and are unlikely to have negative effects*

* listener: fixed the crash when updating listeners that do not bind to port.
* thrift_proxy: fix the thrift_proxy connection manager to correctly report success/error response metrics when performing :ref:`payload passthrough <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.payload_passthrough>`.

Removed Config or Runtime
-------------------------
*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`

* http: removed ``envoy.reloadable_features.treat_upstream_connect_timeout_as_connect_failure`` and legacy code paths.

New Features
------------
* http: added support for :ref:`retriable health check status codes <envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.retriable_statuses>`.

Deprecated
----------
