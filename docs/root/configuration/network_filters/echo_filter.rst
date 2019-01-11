.. _config_network_filters_echo:

Echo
====

The echo is a trivial network filter mainly meant to demonstrate the network filter API. If
installed it will echo (write) all received data back to the connected downstream client. 
This filter should be configured with the name *envoy.echo*.

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`
