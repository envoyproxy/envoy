.. _config_http_filters_on_demand:

按需更新 VHDS 和 S/RDS
================================

如果在过滤器链中配置了这个按需过滤器，则可用于支持按需的 VHDS 或 S/RDS 更新。

如在 :ref:`路由配置 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` 中不存在虚拟主机的信息，则按需更新过滤器可用于请求 :ref:`虚拟主机 <envoy_v3_api_msg_config.route.v3.VirtualHost>` 的数据。
的数据。*Host* 或者 *:authority* 头部的内容可以用于创建按需请求。对于要创建的按需请求， :ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` 必须启用，并且 *Host* 或者 *:authority* 头部要存在。

如果 :ref:`RouteConfiguration 作用域 <envoy_v3_api_msg_config.route.v3.ScopedRouteConfiguration>` 中指定了按需加载 RouteConfiguration，则按需更新过滤器还可以用于请求*路由配置*数据。
HTTP 头部的内容用于查找作用域并创建按需请求。

按需 VHDS 和按需 S/RDS 目前不能同时使用。

配置
-------------
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
* 该过滤器的名称应该配置为 *envoy.filters.http.on_demand*。
* 该过滤器应放在 HttpConnectionManager 的过滤器链中，并在 *envoy.filters.http.router* 过滤器之前。
