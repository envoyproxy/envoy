.. _config_dubbo_filters_router:

Router
======

The router filter implements Dubbo forwarding. It will be used in almost all Dubbo proxying
scenarios. The filter's main job is to follow the instructions specified in the configured
:ref:`route table <envoy_v3_api_msg_extensions.filters.network.dubbo_proxy.v3.RouteConfiguration>`.

* :ref:`v3 API reference <envoy_v3_api_msg_config.filter.thrift.router.v2alpha1.Router>`
* This filter should be configured with the name *envoy.filters.dubbo.router*.
