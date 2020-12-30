.. _config_network_filters_ext_authz:

外部授权
======================

* 外部授权 :ref:`架构概览 <arch_overview_ext_authz>`
* :ref:`网络过滤器 v3 API 参考 <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ExtAuthz>`
* 过滤器的名称应该配置为 *envoy.filters.network.ext_authz* 。

外部授权网络过滤器调用外部授权服务去检查传入请求是否被授权。如果请求被网络过滤器视为未经授权那么连接将会被关闭。

.. tip::
  建议在过滤器链中首先配置此过滤器，以便可以在其他过滤器处理请求之前授权请求。

传递到授权服务的请求内容由 :ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>` 来决定。

.. _config_network_filters_ext_authz_network_configuration:

网络过滤器、gRPC 服务都可以在下面配置。你可以在 :ref:`网络过滤器 <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ExtAuthz>` 上看到所有的配置选项。

示例
-------

一个简单的过滤器配置如下：

.. code-block:: yaml

  filters:
    - name: envoy.filters.network.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.ext_authz.v3.ExtAuthz
        stat_prefix: ext_authz
        grpc_service:
          envoy_grpc:
            cluster_name: ext-authz
        include_peer_certificate: true

  clusters:
    - name: ext-authz
      type: static
      http2_protocol_options: {}
      load_assignment:
        cluster_name: ext-authz
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 10003

统计信息
----------

网络过滤器在 *config.ext_authz.* 命名空间中输出统计信息。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  total, Counter, 来自过滤器的响应总数
  error, Counter, 连接外部服务的异常总数
  denied, Counter, 来自授权服务的拒绝的响应总数
  disabled, Counter, 由于过滤器未启用而未调用外部服务的通过请求总数
  failure_mode_allowed, Counter, 由于 failure_mode_allow 设置为 true 而允许的请求异常总数
  ok, Counter, 来自授权服务的允许通过的响应总数
  cx_closed, Counter, 关闭的连接总数
  active, Gauge, 当前传输到授权服务的活跃请求总数

动态元数据
----------------
.. _config_network_filters_ext_authz_dynamic_metadata:

只有当 gRPC 授权服务返回一个包含 :ref:`检查响应 <envoy_v3_api_msg_service.auth.v3.CheckResponse>` 字段的 :ref:`dynamic_metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` 时，外部授权过滤器才会将动态元数据以不透明的 ``google.protobuf.Struct`` 的形式发出。
