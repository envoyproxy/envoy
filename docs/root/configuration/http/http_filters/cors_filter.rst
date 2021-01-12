.. _config_http_filters_cors:

CORS
====

此过滤器是用来处理那些基于路由或虚拟主机设置的跨域资源请求。关于头部的含义，可以参阅下面的几页。

* https://developer.mozilla.org/en-US/docs/Web/HTTP/Access_control_CORS
* https://www.w3.org/TR/cors/
* :ref:`v2 API 参考 <envoy_v3_api_msg_config.route.v3.CorsPolicy>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.cors* 。

.. _cors-runtime:

运行时
-------
启用了过滤器的请求比例可以通过 :ref:`filter_enabled
<envoy_v3_api_field_config.route.v3.CorsPolicy.filter_enabled>` 字段中的 :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` 值来配置。

仅在影子模式下，启用了过滤器的请求比例可以通过 
:ref:`shadow_enabled <envoy_v3_api_field_config.route.v3.CorsPolicy.shadow_enabled>` 字段中的 :ref:`runtime_key
<envoy_v3_api_field_config.core.v3.RuntimeFractionalPercent.runtime_key>` 值来配置。仅当在影子模式下启用时，过滤器将评估请求的*来源*来决定它是否是有效的，但不会强制执行任何策略。

.. note::

  如果同时打开了 ``filter_enabled`` 和 ``shadow_enabled``， ``filter_enabled`` 标志将优先起作用。

.. _cors-statistics:

统计
-----

CORS 过滤器输出的统计信息在 <stat_prefix>.cors.* 命名空间下。

.. note::
  没有来源头部的请求将不会出现在统计信息中。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  origin_valid, Counter, 具有有效来源头部请求的总数。
  origin_invalid, Counter, 具有无效来源头部请求的总数。
