.. _config_network_filters_echo:

Echo
====

The echo is a trivial network filter mainly meant to demonstrate the network filter API. If
installed it will echo (write) all received data back to the connected downstream client. 
This filter should be configured with the name *envoy.filters.network.echo*.

* :ref:`v3 API reference <envoy_v3_api_field_config.listener.v3.Filter.name>`
