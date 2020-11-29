.. _arch_overview_rbac:

基于角色的权限访问控制
======================

* :ref:`网络过滤器配置 <config_network_filters_rbac>`。
* :ref:`HTTP 过滤器配置 <config_http_filters_rbac>`。

基于角色的权限访问控制（RBAC）过滤器会检查传入请求是否是经过授权的。区别于外部授权的是，RBAC 过滤器的检查是发生在 Envoy 流程中的，是基于一系列来自于过滤器配置的策略。

RBAC 过滤器可以被配置成 :ref:`网络过滤器 <config_network_filters_rbac>`，或者 :ref:`HTTP 过滤器 <config_http_filters_rbac>` 或者同时配置成两者。如果请求被网络过滤器视为是未授权的，则连接将被关闭。如果请求被 HTTP 过滤器视为是未经授权的，则请求会被拒绝并返回 403（禁止）响应。

策略
----

RBAC 过滤器会基于一系列的 :ref:`策略 <envoy_v3_api_field_config.rbac.v3.RBAC.policies>` 来检查请求。一个策略包含一系列的 :ref:`权限 <envoy_v3_api_msg_config.rbac.v3.Permission>` 和 :ref:`主体 <envoy_v3_api_msg_config.rbac.v3.Principal>`。权限说明了请求所做的动作，比如，HTTP 请求的方法和路径。主体则说明了下游客户端是如何识别请求的，比如，下游客户端证书的 URI SAN。如果策略的权限和主体同时被匹配，则说明这个策略被完全匹配。

影子（Shadow）策略
------------------

过滤器可以配置一个 :ref:`影子策略 <envoy_v3_api_field_extensions.filters.http.rbac.v3.RBAC.shadow_rules>`，此策略除了会发出统计信息并记录结果外，没有任何其他作用。这种方式对于将规则应用在生产之前进行测试时是非常有用的。

.. _arch_overview_condition:

条件
----

除了预定义的权限和主体外，策略或许会选择性的提供写在 `通用表达语言 <https://github.com/google/cel-spec/blob/master/doc/intro.md>`_ 中的授权条件。这些条件说明了一些要匹配策略所必须要满足的一些条款。比如，如下的条件会检查请求的路径是否是以 `/v1/` 开始的：

.. code-block:: yaml

  call_expr:
    function: startsWith
    args:
    - select_expr:
       operand:
         ident_expr:
           name: request
       field: path
    - const_expr:
       string_value: /v1/

以下是语言运行时的一些属性：

.. csv-table::
   :header: 属性, 类型, 描述
   :widths: 1, 1, 2

   request.path, string, URL 的路径部分
   request.url_path, string, URL 的路径部分但没有查询字符
   request.host, string, URL 的主机部分
   request.scheme, string, URL 的 schema 部分
   request.method, string, 请求方法
   request.headers, string map, 所有的请求头
   request.referer, string, 引用请求头
   request.useragent, string, 用户代理请求头
   request.time, timestamp, 接受第一个字节的时间
   request.duration, duration, 请求的持续时间
   request.id, string, 请求 ID
   request.size, int, 请求征文的大小
   request.total_size, int, 包含头的请求
   request.protocol, string, 请求协议，比如 “HTTP/2”
   response.code, int, HTTP 响应状态码
   response.code_details, string, 内部响应码详情（可更改）
   response.grpc_status, int, gRPC 响应状态码
   response.headers, string map, 所有响应头部
   response.trailers, string map, 所有响应尾部
   response.size, int, 响应正文大小
   response.total_size, int, 响应的总大小，包括头部和尾部的近似压缩大小
   response.flags, int, 除了标准响应码之外关于响应的额外信息
   source.address, string, 下游连接远端地址
   source.port, int, 下游连接远端端口
   destination.address, string, 下游连接本地地址
   destination.port, int, 下游连接的本地端口
   metadata, :ref:`Metadata<envoy_api_msg_core.Metadata>`, 动态元数据
   filter_state, map string to bytes, 过滤器状态将数据名称映射到他们序列化的字符串值中
   connection.mtls, bool, 用来指示 TLS 是否被应用与下游连接和并显示对等证书
   connection.requested_server_name, string, 下游 TLS 连接中请求的服务器名称
   connection.tls_version, string, 下游 TLS 连接的 TLS 版本
   connection.subject_local_certificate, string, 下游 TLS 连接中本地证书中的主题字段
   connection.subject_peer_certificate, string, 下游 TLS 连接中对端证书中的主题字段
   connection.dns_san_local_certificate, string, 下游 TLS 连接中本地证书 SAN 字段中的第一个 DNS 入口
   connection.dns_san_peer_certificate, string, 下游 TLS 连接中对端证书 SAN 字段中的第一个 URI 入口
   connection.uri_san_local_certificate, string, 下游 TLS 连接中本地证书 SAN 字段中的第一个 URI 入口
   connection.uri_san_peer_certificate, string, 下游 TLS 连接中对端证书 SAN 字段中的第一个 URI 入口
   connection.id, uint, 下游连接 ID
   upstream.address, string, 上游连接远端地址
   upstream.port, int, 上游连接远端端口
   upstream.tls_version, string, 上游 TLS 连接的 TLS 版本
   upstream.subject_local_certificate, string, 上游 TLS 连接中本地证书的主题字段
   upstream.subject_peer_certificate, string, 上游 TLS 连接中对端证书的主题字段
   upstream.dns_san_local_certificate, string, 上游 TLS 连接中本地证书 SAN 字段中的第一个 DNS 入口
   upstream.dns_san_peer_certificate, string, 上游 TLS 连接中对端证书 SAN 字段中的第一个 DNS 入口
   upstream.uri_san_local_certificate, string, 上游 TLS 连接中本地证书 SAN 字段中的第一个 URI 入口
   upstream.uri_san_peer_certificate, string, 上游 TLS 连接中对端证书 SAN 字段中的第一个 URI 入口
   upstream.local_address, string, 上游连接的本地地址
   upstream.transport_failure_reason, string, 上游传输失败原因，比如，证书验证失败


大部分属性都是可选项且提供基于属性类型的默认值。CEL 支持使用 `has()` 语法检查属性的存在，比如 `has(request.referer)`。
