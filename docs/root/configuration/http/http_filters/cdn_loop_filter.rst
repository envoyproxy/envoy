.. _config_http_filters_cdn_loop:

CDN-Loop 头部
===============

CDN-Loop 头部过滤器参与 `RFC 8586 <https://tools.ietf.org/html/rfc8586>`_  定义的 cross-CDN 循环检测协议。
CDN-Loop 头部过滤器有两个作用。首先，过滤器会检测 CDN-Loop 头部中特定的 CDN 标识出现过的次数。
其次，在通过第一步校验后，过滤器会在 CDN-Loop 头部中追加一个 CDN 标识，然后将请求传递给下游过滤器。如果校验失败，
过滤器会终止该请求的处理并返回一个包含错误信息的响应。

RFC 8586 在如何修改 CDN-Loop 头部方面有特别的规定。例如：

* 在过滤器链路上的的其他过滤器不能修改 CDN-Loop 头部，且
* HTTP 路由配置的 :ref:`请求头部增加
  <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_add>`
  或 :ref:`请求头部删除 <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_remove>`
  应不包含 CDN-Loop 头部。

过滤器会将多个 CDN-Loop 头部合并成一个，以逗号分隔。

配置
-------------

此过滤器的名称应该被配置为 *envoy.filters.http.cdn_loop* 。

`过滤器配置 <config_http_filters_cdn_loop>`_ 有两个字段。

* *cdn_id* 字段设置过滤器查找和追加在 CDN-Loop 头部中的标识； RFC 8586 称该字段为 "cdn-id";
  "cdn-id" 可以是 CDN 提供的、可控的化名或主机名。*cdn_id* 字段必须非空。

* *max_allowed_occurrences* 字段控制下游请求 CDN-Loop 头部中 *cdn_id* 可以出现的次数。
  （在过滤器追加 *cdn_id* 到头部前） 如果头部中 *cdn_id* 出现的次数大于 *max_allowed_occurrences*，
  过滤器将拒绝此次下游请求。大多数用户应将 *max_allowed_occurrences* 设置为 0 （默认值）。

响应码详情
---------------------

.. list-table::
   :header-rows: 1

   * - 名称
     - HTTP 状态
     - 描述
   * - invalid_cdn_loop_header
     - 400 （错误的请求）
     - 下游系统的 CDN-Loop 头部无效或无法转义。
   * - cdn_loop_detected
     - 502 （错误网关）
     - CDN-Loop 头部中 *cdn_id* 的值超过了 *max_allowed_occurrences*，说明在 CDN 中存在循环。