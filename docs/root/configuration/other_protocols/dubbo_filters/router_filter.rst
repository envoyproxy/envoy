.. _config_dubbo_filters_router:

路由
=====

路由过滤器实现了 Dubbo 转发。几乎所有的 Dubbo 代理场景中都会用到它。过滤器的主要工作是遵从配置在 :ref:`路由表 <envoy_v3_api_msg_extensions.filters.network.dubbo_proxy.v3.RouteConfiguration>` 中指定的指令。


* :ref:`v3 API 参考 <envoy_v3_api_msg_config.filter.thrift.router.v2alpha1.Router>`
* 此过滤器的名称应该被配置为 *envoy.filters.dubbo.router*。
