.. _config_network_filters_echo:

Echo
====

The echo is a trivial network filter mainly meant to demonstrate the network filter API. If
installed it will echo (write) all received data back to the connected downstream client.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.echo.v3.Echo>`
