.. _config_http_conn_man_header_sanitizing:

HTTP 头修正
======================

出于安全原因，Envoy 会根据请求来自内部或外部来“修正”各种 HTTP 头。修正操作取决于请求头，并可能对其执行增加、移除或修改等操作。最终，将根据请求头 :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` 来判断请求来自内部或外部（请仔细阅读链接的内容，因为 Envoy 填充请求头的过程比较复杂，且该过程取决于请求头 :ref:`use_remote_address
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.use_remote_address>` 的设置）。另外，请求头 :ref:`internal_address_config
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.internal_address_config>` 可以用来设置内部/外部请求的判断。

Envoy 可能对下列请求头执行修正操作：

* :ref:`x-envoy-decorator-operation <config_http_filters_router_x-envoy-decorator-operation>`
* :ref:`x-envoy-downstream-service-cluster
  <config_http_conn_man_headers_downstream-service-cluster>`
* :ref:`x-envoy-downstream-service-node <config_http_conn_man_headers_downstream-service-node>`
* :ref:`x-envoy-expected-rq-timeout-ms <config_http_filters_router_x-envoy-expected-rq-timeout-ms>`
* :ref:`x-envoy-external-address <config_http_conn_man_headers_x-envoy-external-address>`
* :ref:`x-envoy-force-trace <config_http_conn_man_headers_x-envoy-force-trace>`
* :ref:`x-envoy-internal <config_http_conn_man_headers_x-envoy-internal>`
* :ref:`x-envoy-ip-tags <config_http_filters_ip_tagging>`
* :ref:`x-envoy-max-retries <config_http_filters_router_x-envoy-max-retries>`
* :ref:`x-envoy-retry-grpc-on <config_http_filters_router_x-envoy-retry-grpc-on>`
* :ref:`x-envoy-retry-on <config_http_filters_router_x-envoy-retry-on>`
* :ref:`x-envoy-upstream-alt-stat-name <config_http_filters_router_x-envoy-upstream-alt-stat-name>`
* :ref:`x-envoy-upstream-rq-per-try-timeout-ms
  <config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms>`
* :ref:`x-envoy-upstream-rq-timeout-alt-response
  <config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response>`
* :ref:`x-envoy-upstream-rq-timeout-ms <config_http_filters_router_x-envoy-upstream-rq-timeout-ms>`
* :ref:`x-forwarded-client-cert <config_http_conn_man_headers_x-forwarded-client-cert>`
* :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`
* :ref:`x-forwarded-proto <config_http_conn_man_headers_x-forwarded-proto>`
* :ref:`x-request-id <config_http_conn_man_headers_x-request-id>`
