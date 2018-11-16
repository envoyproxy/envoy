.. _well_known_dynamic_metadata:

Well Known Dynamic Metadata
===========================

Filters can emit dynamic metadata via the *setDynamicMetadata* routine in the
:repo:`StreamInfo <include/envoy/stream_info/stream_info.h>` interface on a
:repo:`Connection <include/envoy/network/connection.h>`. This metadata emitted by a filter can be
consumed by other filters and useful features can be built by stacking such filters. For example,
a logging filter can consume dynamic metadata from an RBAC filter to log details about runtime
shadow rule behavior. Another example is where an RBAC filter permits/restricts MongoDB operations
by looking at the operational metadata emitted by the MongoDB filter.

The following Envoy filters emit dynamic metadata that other filters can leverage.

* :ref:`Mongo Proxy Filter <config_network_filters_mongo_proxy_dynamic_metadata>`
* :ref:`Role Based Access Control (RBAC) Filter <config_http_filters_rbac_dynamic_metadata>`
* :ref:`Role Based Access Control (RBAC) Network Filter <config_network_filters_rbac_dynamic_metadata>`
