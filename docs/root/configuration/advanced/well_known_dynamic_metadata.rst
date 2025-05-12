.. _well_known_dynamic_metadata:

Well Known Dynamic Metadata
===========================

Filters can emit dynamic metadata via the *setDynamicMetadata* routine in the
:repo:`StreamInfo <envoy/stream_info/stream_info.h>` interface on a
:repo:`Connection <envoy/network/connection.h>`. This metadata emitted by a filter can be
consumed by other filters and useful features can be built by stacking such filters. For example,
a logging filter can consume dynamic metadata from an RBAC filter to log details about runtime
shadow rule behavior. Another example is where an RBAC filter permits/restricts MySQL/MongoDB operations
by looking at the operational metadata emitted by the MongoDB filter.

The following Envoy filters emit dynamic metadata that other filters can leverage.

* :ref:`External Authorization Filter <config_http_filters_ext_authz_dynamic_metadata>`
* :ref:`External Authorization Network Filter <config_network_filters_ext_authz_dynamic_metadata>`
* :ref:`Header-To-Metadata Filter <config_http_filters_header_to_metadata>`
* :ref:`JWT Authentication Filter <config_http_filters_jwt_authn>` for the extracted
  :ref:`header <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.payload_in_metadata>`
  and :ref:`payload <envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtProvider.payload_in_metadata>`
* :ref:`Mongo Proxy Filter <config_network_filters_mongo_proxy_dynamic_metadata>`
* :ref:`MySQL Proxy Filter <config_network_filters_mysql_proxy_dynamic_metadata>`
* :ref:`Postgres Proxy Filter <config_network_filters_postgres_proxy_dynamic_metadata>`
* :ref:`Role Based Access Control (RBAC) Filter <config_http_filters_rbac_dynamic_metadata>`
* :ref:`Role Based Access Control (RBAC) Network Filter <config_network_filters_rbac_dynamic_metadata>`
* :ref:`ZooKeeper Proxy Filter <config_network_filters_zookeeper_proxy_dynamic_metadata>`

The following Envoy filters can be configured to consume dynamic metadata emitted by other filters.

* :ref:`External Authorization Filter via the metadata context namespaces
  <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.metadata_context_namespaces>`
* :ref:`RateLimit Filter limit override <config_http_filters_rate_limit_override_dynamic_metadata>`
* :ref:`Original destination listener filter <config_listener_filters_original_dst>`

.. _shared_dynamic_metadata:

Shared Dynamic Metadata
-----------------------
Dynamic metadata that is set by multiple filters is placed in the common key namespace ``envoy.common``. Refer to the corresponding rules when setting this metadata.

.. csv-table::
  :header: Name, Type, Description, Rules
  :widths: 1, 1, 3, 3

  access_log_hint, boolean, Whether access loggers should log the request., "When this metadata is already set: A ``true`` value should not be overwritten by a ``false`` value, while a ``false`` value can be overwritten by a ``true`` value."

The following Envoy filters emit shared dynamic metadata.

* :ref:`Role Based Access Control (RBAC) Filter <config_http_filters_rbac_dynamic_metadata>`
* :ref:`Role Based Access Control (RBAC) Network Filter <config_network_filters_rbac_dynamic_metadata>`

The following filters consume shared dynamic metadata.

* :ref:`Metadata Access Log Filter<envoy_v3_api_msg_config.accesslog.v3.MetadataFilter>`
