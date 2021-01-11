.. _config_http_filters_header_to_metadata:

Envoy Header-To-Metadata 过滤器
=======================================
* :ref:`v3 API 参考 <envoy_v3_api_msg_extensions.filters.http.header_to_metadata.v3.Config>`
* 此过滤器的名称应该被配置为 *envoy.filters.http.header_to_metadata*。

该过滤器需要配置规则来匹配请求和响应。
每条规则有一个 cookie 或一个头部，当该 cookie 或头部存在或丢失时触发规则。

当规则被触发时，动态元数据会被添加到基于规则的配置中。
如果头部或 cookie 存在，它们的值会被提取出来，和指定的键一起被用作元数据。如果头部或 cookie 不存在，则触发缺省场景，并使用缺省值作为元数据。

可以使用元数据来支持负载均衡决策、日志消费等等。

该过滤器的典型应用场景是通过动态匹配将请求分发给负载均衡子组。做法是从头部里抓取指定值并附加到请求的动态元数据里，然后匹配到端点子组上。

示例
--------

通过检查头部里是否存在版本标记来路由请求到相应端点的配置如下：

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - header: x-version
            on_header_present:
              metadata_namespace: envoy.lb
              key: version
              type: STRING
            on_header_missing:
              metadata_namespace: envoy.lb
              key: default
              value: 'true'
              type: STRING
            remove: false

和头部类似，也可以通过抓取请求中指定 cookie 值与指定键来作为元数据。
不支持匹配到规则后删除 cookie。

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - cookie: cookie
            on_header_present:
              metadata_namespace: envoy.lb
              key: version
              type: STRING
            on_header_missing:
              metadata_namespace: envoy.lb
              key: default
              value: 'true'
              type: STRING
            remove: false

与之对应的上游集群配置如下：

.. code-block:: yaml

  clusters:
    - name: versioned-cluster
      type: EDS
      lb_policy: ROUND_ROBIN
      lb_subset_config:
        fallback_policy: ANY_ENDPOINT
	subset_selectors:
	  - keys:
	      - default
          - keys:
	      - version

头部里设置了 `x-version` 的请求会被匹配到指定版本的端点。而未设置该头部的请求会被匹配到默认端点。

如果需要先将头部里的值做转换再加入动态元数据，这个过滤器也支持正则匹配和替换：

.. code-block:: yaml

  http_filters:
    - name: envoy.filters.http.header_to_metadata
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
        request_rules:
          - header: ":path"
            on_header_present:
              metadata_namespace: envoy.lb
              key: cluster
              regex_value_rewrite:
                pattern:
                  google_re2: {}
                  regex: "^/(cluster[\\d\\w-]+)/?.*$"
                substitution: "\\1"

注意这个过滤器也支持按路由分别配置：

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/version-to-metadata" }
        route: { cluster: service }
        typed_per_filter_config:
          envoy.filters.http.header_to_metadata:
            "@type": type.googleapis.com/envoy.extensions.filters.http.header_to_metadata.v3.Config
            request_rules:
              - header: x-version
                on_header_present:
                  metadata_namespace: envoy.lb
                  key: version
                  type: STRING
                remove: false
      - match: { prefix: "/" }
        route: { cluster: some_service }

This can be used to either override the global configuration or if the global configuration
is empty (no rules), it can be used to only enable the filter at a per route level.
可以使用这种方式来覆盖全局配置，或全局配置留空（没有设置规则），也可以用来启用按路由级别的过滤器。

统计数据
--------------

目前，这个过滤器尚未提供统计数据。
