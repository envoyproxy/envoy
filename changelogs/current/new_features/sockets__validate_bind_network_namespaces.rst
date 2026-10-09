Added a new :ref:`validate_network_namespaces
<envoy_v3_api_field_config.core.v3.BindConfig.validate_network_namespaces>` option to
``BindConfig``. When set, the :ref:`network_namespace_filepath
<envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>` of every source
address in the bind config is validated at configuration load time, and the configuration is
rejected if a referenced Linux network namespace cannot be opened.
