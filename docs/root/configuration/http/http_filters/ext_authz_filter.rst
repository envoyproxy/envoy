.. _config_http_filters_ext_authz:

外部授权
======================
* 外部授权 :ref:`架构概览 <arch_overview_ext_authz>`
* :ref:`HTTP 过滤器 v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>`
* 过滤器的名称应该配置为 *envoy.filters.http.ext_authz* 。

外部授权服务通过调用外部 gRPC 或者 HTTP 服务来检查传入的 HTTP 请求是否被授权。
如果该请求被视为未授权，则通常会以 403 （禁止）响应拒绝该请求。
注意，从授权服务向上游、下游或者授权服务发送其他自定义元数据也是被允许的。在 :ref:`HTTP 过滤器 <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>` 中有更多详细的解释。

传递给授权服务的请求内容由 :ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>` 指定。

.. _config_http_filters_ext_authz_http_configuration:

使用 gRPC/HTTP 服务的 HTTP 过滤器配置如下。你可以在 :ref:`HTTP 过滤器 <envoy_v3_api_msg_extensions.filters.http.ext_authz.v3.ExtAuthz>` 看到所有的配置选项。

配置示例
----------------------

gRPC 授权服务器的过滤器配置示例：

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
        grpc_service:
          envoy_grpc:
            cluster_name: ext-authz

          # Default is 200ms; override if your server needs e.g. warmup time.
          timeout: 0.5s
        include_peer_certificate: true

.. code-block:: yaml

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

      # This timeout controls the initial TCP handshake timeout - not the timeout for the
      # entire request.
      connect_timeout: 0.25s

.. note::

  这个过滤器的一个特性就是将 HTTP 请求体作为 :ref:`检查请求 <envoy_v3_api_msg_service.auth.v3.CheckRequest>` 的一部分发送到配置的 gRPC 授权服务器。

  简单配置如下：

  .. code:: yaml

    http_filters:
      - name: envoy.filters.http.ext_authz
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
          grpc_service:
            envoy_grpc:
              cluster_name: ext-authz
          with_request_body:
            max_request_bytes: 1024
            allow_partial_message: true
            pack_as_bytes: true

  注意，默认情况下，:ref:`check request<envoy_v3_api_msg_service.auth.v3.CheckRequest>` 以 UTF-8 字符串的形式携带 HTTP 请求体，并同时填充 :ref:`body <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.body>` 字段。
  如果需要将请求体打包为原始字节，则需要将 :ref:`pack_as_bytes <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.BufferSettings.pack_as_bytes>` 设置为 true。
  事实上，:ref:`raw_body <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.raw_body>` 字段会被赋值，而 :ref:`body <envoy_v3_api_field_service.auth.v3.AttributeContext.HttpRequest.body>` 会被设为空。

原始 HTTP 授权服务器的过滤器配置示例：

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.ext_authz
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
        http_service:
            server_uri:
              uri: 127.0.0.1:10003
              cluster: ext-authz
              timeout: 0.25s
              failure_mode_allow: false
        include_peer_certificate: true

.. code-block:: yaml

  clusters:
    - name: ext-authz
      connect_timeout: 0.25s
      type: logical_dns
      lb_policy: round_robin
      load_assignment:
        cluster_name: ext-authz
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: 127.0.0.1
                  port_value: 10003

按路由独立配置
-----------------------

虚拟主机和路由过滤器的简单配置示例。
在示例中，我们为虚拟主机添加了其他的上下文，并且禁用了前缀为 `/static` 的路由过滤器。

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      typed_per_filter_config:
        envoy.filters.http.ext_authz:
          "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute
          check_settings:
            context_extensions:
              virtual_host: local_service
      routes:
      - match: { prefix: "/static" }
        route: { cluster: some_service }
        typed_per_filter_config:
          envoy.filters.http.ext_authz:
            "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthzPerRoute
            disabled: true
      - match: { prefix: "/" }
        route: { cluster: some_service }

统计信息
----------
.. _config_http_filters_ext_authz_stats:

HTTP 过滤器输出的统计信息 *cluster.<route target cluster>.ext_authz.* 命名空间中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  ok, Counter, 过滤器的响应总数。
  error, Counter, 联系外部服务（包含超时）的异常总数。
  timeout, Counter, 联系外部服务的超时总数（仅计算在创建请求时判定为超时）。
  denied, Counter, 授权服务的拒绝通信的响应总数。
  disabled, Counter, 由于过滤器被禁用，不调用外部服务而允许的请求总数。
  failure_mode_allowed, Counter, 出现异常但由于 failure_mode_allow 被设置为 true 而允许通过的请求总数。

动态元数据
----------------
.. _config_http_filters_ext_authz_dynamic_metadata:

.. note::

  外部授权服务器仅在使用 gRPC 服务作为授权服务器时才会发出动态元数据。

当 gRPC 授权服务器返回一个带有 :ref:`dynamic_metadata <envoy_v3_api_field_service.auth.v3.CheckResponse.dynamic_metadata>` 字段的 :ref:`CheckResponse <envoy_v3_api_msg_service.auth.v3.CheckResponse>` 时，外部授权过滤器会将动态元数据作为不透明的 ``google.protobuf.Struct`` 发出。

运行时
-------
可以通过 :ref:`filter_enabled <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.filter_enabled>` 字段的 :ref:`runtime_key <envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` 值来配置启用过滤器的请求百分比。
