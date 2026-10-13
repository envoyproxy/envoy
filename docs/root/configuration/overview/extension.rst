.. _config_overview_extension_configuration:

Extension configuration
-----------------------

Each configuration resource in Envoy has a type URL in the ``typed_config``. This
type corresponds to a versioned schema. The type URL uniquely identifies an
extension capable of interpreting the configuration. The ``name`` field is
optional and can be used as an identifier or as an annotation for the
particular instance of the extension configuration. For example, the following
filter configuration snippet is permitted:

.. literalinclude:: _include/extensions/without-type-url.yaml
   :language: yaml
   :lines: 14-33
   :linenos:
   :lineno-start: 14
   :caption: :download:`extensions-demo.yaml <_include/extensions/without-type-url.yaml>`

In case the control plane lacks the schema definitions for an extension,
``xds.type.v3.TypedStruct`` should be used as a generic container. The type URL
inside it is then used by a client to convert the contents to a typed
configuration resource. For example, the above example could be written as
follows:

.. literalinclude:: _include/extensions/with-type-url.yaml
   :language: yaml
   :lines: 14-37
   :linenos:
   :lineno-start: 14
   :caption: :download:`extensions-demo-with-type-url.yaml <_include/extensions/with-type-url.yaml>`

.. _config_overview_extension_discovery:

Discovery service
^^^^^^^^^^^^^^^^^

Extension configuration can be supplied dynamically from an :ref:`xDS
management server<xds_protocol>` using :ref:`ExtensionConfiguration discovery
service<envoy_v3_api_file_envoy/service/extension/v3/config_discovery.proto>`.
The name field in the extension configuration acts as the resource identifier.

Listener filters
^^^^^^^^^^^^^^^^

For Listener filters, the discovery service configuration is: :ref:`dynamic listener filter
re-configuration<envoy_v3_api_field_config.listener.v3.ListenerFilter.config_discovery>`.
The dynamic listener filter config is only supported in TCP listeners.
If the dynamic configuration is missing, the connection will be rejected until a valid config is updated.

Network filters
^^^^^^^^^^^^^^^

For downstream network filters, the discovery service configuration is: :ref:`dynamic filter
re-configuration<envoy_v3_api_field_config.listener.v3.Filter.config_discovery>`.
If the dynamic configuration is missing, the connection will be rejected until a valid config is updated.
When a filter configuration updates, the new configuration will only apply to new connections, existing connections
will keep using the older filter configuration.

HTTP filters
^^^^^^^^^^^^

For HTTP filters, HTTP connection manager supports :ref:`dynamic filter
re-configuration<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpFilter.config_discovery>`.
If the configuration is missing a local HTTP response with '500' status code will be returned.

UDP Session filters
^^^^^^^^^^^^^^^^^^^

For UDP session filters, UDP proxy supports :ref:`dynamic filter
re-configuration<envoy_v3_api_field_extensions.filters.udp.udp_proxy.v3.UdpProxyConfig.SessionFilter.config_discovery>`.
If the configuration is missing the UDP session will be removed

Statistics
^^^^^^^^^^

In addition to the :ref:`statistics<subscription_statistics>` supported for xDS subscriptions, the following statistics
are supported for listener filters, downstream network filters, and HTTP filters, rooted at *extension_config_discovery.<stat_prefix>.<extension_config_name>*.

- For TCP listener filters, the value of *<stat_prefix>* is *tcp_listener_filter*.
- For downstream network filters, the value of *<stat_prefix>* is *network_filter*.
- For upstream network filters, the value of *<stat_prefix>* is *upstream_network_filter*.
- For downstream HTTP filters, the value of *<stat_prefix>* is *http_filter*.
- For upstream HTTP filters, the value of *<stat_prefix>* is *upstream_http_filter*.
- For UDP session filters the value of *<stat_prefix>* is *udp_session_filter*.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  config_reload, Counter, Total number of successful configuration updates
  config_fail, Counter, Total number of failed configuration updates
  config_conflict, Counter, Total number of conflicting applications of configuration updates; this may happen when a new listener cannot reuse a subscribed extension configuration due to an invalid type URL.

Additionally, the following statistics are supported to indicate that a connection was closed due to a missing
configuration, rooted at listener.<address> (or listener.<stat_prefix>. if stat_prefix is non-empty).

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  extension_config_missing, Counter, Total connections closed due to missing listener filter extension configuration
  network_extension_config_missing, Counter, Total connections closed due to missing network filter extension configuration
