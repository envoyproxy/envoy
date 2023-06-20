.. _config_dubbo_filters_router:

Router
======

The router filter implements Dubbo forwarding. It will be used in almost all Dubbo proxying
scenarios. The filter's main job is to follow the instructions specified in the configured
:ref:`route table <envoy_v3_api_msg_extensions.filters.network.dubbo_proxy.v3.RouteConfiguration>`.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.dubbo_proxy.router.v3.router``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.dubbo_proxy.router.v3.router>`
