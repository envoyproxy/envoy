.. _well_known_dynamic_metadata:

众所周知的动态元数据
=====================

过滤器可以通过 *setDynamicMetadata* 来发送动态元数据，而通常在 :repo:`连接 <include/envoy/network/connection.h>` 中的 :repo:`StreamInfo <include/envoy/stream_info/stream_info.h>` 接口中。过滤器发送的元数据可以被其他过滤器所消费，可以通过级联这种过滤器来构建有用的特性。比如，一个日志过滤器可以消费来自 RBAC 过滤器的动态元数据，以此来记录运行时影子规则的详细日志信息。另外一个例子是 RBAC 过滤器通过查看由 MongoDB 过滤器发出的操作元数据，来对 MySQL/MongoDB 的操作作出许可/限制。

如下 Envoy 过滤器发出动态元数据，然后数据被其他过滤器所使用。

* :ref:`外部授权过滤器 <config_http_filters_ext_authz_dynamic_metadata>`
* :ref:`外部授权网络过滤器 <config_network_filters_ext_authz_dynamic_metadata>`
* :ref:`Mongo 代理过滤器 <config_network_filters_mongo_proxy_dynamic_metadata>`
* :ref:`MySQL 代理过滤器 <config_network_filters_mysql_proxy_dynamic_metadata>`
* :ref:`Postgres 代理过滤器 <config_network_filters_postgres_proxy_dynamic_metadata>`
* :ref:`基于角色的访问控制（RBAC）过滤器 <config_http_filters_rbac_dynamic_metadata>`
* :ref:`基于角色的访问控制（RBAC）网络过滤器 <config_network_filters_rbac_dynamic_metadata>`
* :ref:`ZooKeeper 代理过滤器 <config_network_filters_zookeeper_proxy_dynamic_metadata>`

如下 Envoy 过滤器可以通过配置来消费由其他过滤器发出动态元数据。

* :ref:`使用元数据上下文命名空间的外部授权过滤器
  <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.metadata_context_namespaces>`
* :ref:`限制覆盖的 RateLimit 过滤器 <config_http_filters_rate_limit_override_dynamic_metadata>`

.. _shared_dynamic_metadata:

共享动态元数据
---------------
通过多个过滤器设置的动态元数据位于公共的密钥命名空间（common key namespace） `envoy.common` 中。当设置此类元数据时，可查看与其对应的规则。

.. csv-table::
  :header: 名称, 类型, 描述, 规则
  :widths: 1, 1, 3, 3

  access_log_hint, boolean, 访问记录器是否要记录请求。, "当此元数据已经设置为：`true` 值不应该被 `false` 值覆盖，而 `false` 值可以被 `true` 覆盖。"

如下 Envoy 过滤器发出共享动态元数据。

* :ref:`基于角色的访问控制（RBAC）过滤器 <config_http_filters_rbac_dynamic_metadata>`
* :ref:`基于角色的访问控制（RBAC）网络过滤器 <config_network_filters_rbac_dynamic_metadata>`

如下 Envoy 过滤器消费共享动态元数据。

* :ref:`元数据访问日志过滤器 <envoy_v3_api_msg_config.accesslog.v3.MetadataFilter>`
