.. _config_http_filters_grpc_json_transcoder:

gRPC-JSON 转码器
====================

* gRPC :ref:`架构概览 <arch_overview_grpc>`
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.grpc_json_transcoder.v3.GrpcJsonTranscoder>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.grpc_json_transcoder*。

这是一个过滤器，它允许 RESTful JSON API 客户端通过 HTTP 向 Envoy 发送请求并代理到 gRPC 服务。gRPC 服务的 HTTP 映射必须由 `自定义选项 <https://cloud.google.com/service-management/reference/rpc/google.api#http>`_ 定义。

JSON 映射
------------

从 protobuf 到 JSON 的映射在`这里 <https://developers.google.com/protocol-buffers/docs/proto3#json>`_ 定义。对于 gRPC 流请求参数，Envoy 需要一个消息数组，并为流响应参数返回一个消息数组。

.. _config_grpc_json_generate_proto_descriptor_set:

如何生成 proto 描述符集
------------------------

Envoy 必须知道你的 gRPC 服务的 proto 描述符才能进行转码。

要为 gRPC 服务生成 protobuf 描述符集，你还需要在运行 protoc 之前从 GitHub 克隆 googleapis，因为在 include 路径中需要 annotations.proto 来定义 HTTP 映射。

.. code-block:: bash

  git clone https://github.com/googleapis/googleapis
  GOOGLEAPIS_DIR=<your-local-googleapis-folder>

然后运行 protoc 从 bookstore.proto 生成描述符集：

.. code-block:: bash

  protoc -I$(GOOGLEAPIS_DIR) -I. --include_imports --include_source_info \
    --descriptor_set_out=proto.pb test/proto/bookstore.proto

如果你有多个 proto 源文件，则可以通过一个命令传递所有这些源文件。

转码请求的路由配置
-------------------

与 gRPC-JSON 转码器一起使用的路由配置应与 gRPC 路由完全相同。由转码过滤器处理的请求将具有 `/<package>.<service>/<method>` 路径和 POST 方法。这些请求的路由配置应与 `/<package>.<service>/<method>` 匹配，而不和传入的请求路径匹配。这样就可以允许路由同时用于 gRPC 请求和 gRPC-JSON 转码请求。

例如，在下面的 proto 示例中，路由器将处理 `/helloworld.Greeter/SayHello` 作为路径，因此路由配置前缀 `/say` 将不匹配对 `SayHello` 的请求。如果要匹配传入的请求路径，请将 `match_incoming_request_route` 设置为 true。

.. code-block:: proto

  package helloworld;

  // The greeting service definition.
  service Greeter {
    // Sends a greeting
    rpc SayHello (HelloRequest) returns (HelloReply) {
      option (google.api.http) = {
        get: "/say"
      };
    }
  }

发送任意内容
--------------

默认情况下，发生转码时，gRPC-JSON 将 gRPC 服务方法的消息输出编码为 JSON，并将 HTTP 响应 `Content-Type` 头部设置为 `application/json`。要发送任意内容，gRPC 服务方法可以使用 `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_ 作为其输出消息类型。该实现需要相应地设置 `content_type <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L68>`_ （设置 HTTP 响应 `Content-Type` 头部的值）和 `data <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto#L71>`_（设置 HTTP 响应体）。gRPC 服务器可以在流传输的情况下发送多个 `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_ 。在这种情况下，HTTP 响应头部 `Content-Type` 将使用第一个 `google.api.HttpBody <https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto>`_ 中的 `content-type`。

头部
-----

gRPC-JSON 将以下头部转发到 gRPC 服务器：

* `x-envoy-original-path`, 包含源 HTTP 请求路径的值
* `x-envoy-original-method`, 包含源 HTTP 请求方法的值

Envoy 配置示例
-----------------

这是一个代理到运行在 localhost:50051 gRPC 服务器的 Envoy 示例配置。端口 51051 代理 gRPC 请求，并使用 gRPC-JSON 转码过滤器提供 RESTful JSON 映射。即你可以向 localhost:50051 发出 gRPC 或 RESTful JSON 请求。

.. literalinclude:: _include/grpc-transcoder-filter.yaml
    :language: yaml
