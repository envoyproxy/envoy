.. _config_network_filters_dubbo_proxy:

Dubbo 代理
============

Dubbo 代理过滤器解码 dubbo 客户端与服务端之间的 RPC 协议。解码的信息会被转换为元数据。元数据主要包含基础请求 ID、请求类型、序列化类型以及路由所需要的服务名称、方法名称、参数名称和参数值。

* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.dubbo_proxy.v3.DubboProxy>`
* 过滤器的名称应该被配置为 *envoy.filters.network.dubbo_proxy* 。

.. _config_network_filters_dubbo_proxy_stats:

统计
------

每个配置的 dubbo 代理过滤器统计以 *dubbo.<stat_prefix>.* 为跟，统计信息如下：

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  request, Counter, 请求总数
  request_twoway, Counter, 双向请求总数
  request_oneway, Counter, 单向请求总数
  request_event, Counter, 事件请求总数
  request_decoding_error, Counter, 解码异常请求总数
  request_decoding_success, Counter, 解码成功请求总数
  request_active, Gauge, 活跃请求总数
  response, Counter, 响应总数
  response_success, Counter, 成功响应总数
  response_error, Counter, 协议解析异常的响应总数
  response_error_caused_connection_close, Counter, 下游关闭连接导致的响应总数
  response_business_exception, Counter, 协议包含业务层返回异常的响应总数
  response_decoding_error, Counter, 解码异常响应总数
  response_decoding_success, Counter, 解码成功响应总数
  response_error, Counter, 协议解析异常响应总数
  local_response_success, Counter, 本地响应总数
  local_response_error, Counter, 本地编码异常响应总数
  local_response_business_exception, Counter, 本地协议包含业务异常的相应总数
  cx_destroy_local_with_active_rq, Counter, 本地销毁的有活跃查询的连接总数
  cx_destroy_remote_with_active_rq, Counter, 远端销毁的有活跃查询的连接总数


基于 dubbo 代理过滤器实现自定义过滤器
---------------------------------------

如果你想要基于 dubbo 协议实现一个自定义过滤器，dubbo 过滤器特提供了类似于 HTTP 的一种非常方便的扩展方式，首先第一步就是实现 DecoderFilter 接口，并给出过滤器的名称，例如 testFilter，第二步就是添加你自己的配置，配置方法可以参考下面的示例：

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.dubbo_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.dubbo_proxy.v3.DubboProxy
        stat_prefix: dubbo_incomming_stats
        protocol_type: Dubbo
        serialization_type: Hessian2
        route_config:
          name: local_route
          interface: org.apache.dubbo.demo.DemoService
          routes:
          - match:
              method:
                name:
                  exact: sayHello
            route:
              cluster: user_service_dubbo_server
        dubbo_filters:
        - name: envoy.filters.dubbo.testFilter
          typed_config:
            "@type": type.googleapis.com/google.protobuf.Struct
            value:
              name: test_service
        - name: envoy.filters.dubbo.router
