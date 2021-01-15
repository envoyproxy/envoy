.. _config_tools_router_check_tool:

路由表检查工具
======================

.. note::

  以下配置仅用于路由表检查工具，不是 Envoy 二进制文件的一部分。
  路由表检查工具是一个独立的二进制文件，可用于验证给定配置文件的 Envoy 路由。

以下指定了路由表检查工具的输入。路由表检查工具检查 :ref:`路由器 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` 返回的路由是否符合预期。该工具可用于检查集群名称、虚拟集群名称、虚拟主机名称、手动路径重写、手动主机重写、路径重定向和头部字段匹配。可以添加其他测试用例的扩展。可以在 :ref:`安装 <install_tools_route_table_check_tool>` 找到有关安装工具和示例工具输入/输出的详细信息。

路由表检查工具配置由一个 json 测试对象数组组成。每个测试对象由三部分组成。

测试名称
  该字段指定每个测试对象的名称。

输入值
  该字段指定要传递到路由器的参数。比如包括 :authority、:path 和 :method 头部字段。:authority 和 :path 字段指定了发送到路由器的 URL，这是必需的。所有其他字段都是可选的。

验证
  该字段指定要检查的预期值和测试用例。至少需要一个测试用例。

有一个测试用的简单工具的 json 配置如下。该测试期望集群名称匹配“instant-server”。::

   tests
   - test_name: Cluster_name_test,
     input:
       authority: api.lyft.com,
       path: /api/locations
     validate:
       cluster_name: instant-server

.. code-block:: yaml

  tests
  - test_name: ...,
    input:
      authority: ...,
      path: ...,
      method: ...,
      internal: ...,
      random_value: ...,
      ssl: ...,
      runtime: ...,
      additional_request_headers:
        - key: ...,
          value: ...
      additional_response_headers:
        - key: ...,
          value: ...
    validate:
      cluster_name: ...,
      virtual_cluster_name: ...,
      virtual_host_name: ...,
      host_rewrite: ...,
      path_rewrite: ...,
      path_redirect: ...,
      request_header_matches:
        - name: ...,
          exact_match: ...
      response_header_matches:
        - name: ...,
          exact_match: ...
        - name: ...,
          presence_match: ...

test_name
  *(required, string)* 测试对象的名称。

input
  *(required, object)* 发送到路由器的输入值，用于决定返回的路由。

  authority
    *(required, string)* authority url。该值与 path 参数一起定义了要匹配的 url。 例如 “api.lyft.com”。

  path
    *(required, string)* path url。 例如 “/foo”。

  method
    *(required, string)* request method。如果未指定，则默认方法为 GET。 选项为 GET、PUT 或 POST。

  internal
    *(optional, boolean)* 一个标志，决定是否将 x-envoy-internal 设置为 true。如果未指定，或者 internal 等于 false，则不设置 x-envoy-internal。

  random_value
    *(optional, integer)* 一个整数，用于确定要选择加权的集群目标，并且作为一个路由引擎决定基于运行时的路由是否生效的因素。
    random_value 的默认值为 0。对于运行时分数分子为 0 的路由，路由检查器工具会将分子更改为 1，以便可以将 random_value 设置为 0 来模拟路由被启用，将 random_value 设置为 int >= 1 的任何值以模拟路由被禁用。

  ssl
    *(optional, boolean)* 一个标志，决定是将 x-forwarded-proto 设置为 https 还是 http。通过将 x-forwarded-proto 设置为给定的协议，该工具能够模拟客户端通过 http 或 https 发出请求的行为。默认情况下，ssl 为 false，对应 x-forwarded-proto 设置为 http。

  runtime
    *(optional, string)* 一个字符串，表示要为测试启用的运行时设置。路由器使用运行时设置以及 random_value 来决定是否应启用路由。只有小于路由条目上定义的分数百分比的 random_value 才能启用路由。

  additional_request_headers, additional_response_headers
    *(optional, array)*  要添加的附加头部作为路由确定的输入。"authority"、
    "path"、"method"、"x-forwarded-proto" 和 "x-envoy-internal" 字段由其他配置选项指定，不应该在这里设置。

    key
      *(required, string)* 要添加的头部字段的名称。

    value
      *(required, string)* 要添加的头部字段的值。

validate
  *(required, object)* validate 对象指定要匹配的返回路由参数。必须至少指定一个测试参数。使用 “”（空字符串）表示没有返回值。例如，要测试没有群集匹配，请使用 {"cluster_name": ""}。

  cluster_name
    *(optional, string)* 匹配集群名称。

  virtual_cluster_name
    *(optional, string)* 匹配虚拟集群名称。

  virtual_host_name
    *(optional, string)* 匹配虚拟主机名。

  host_rewrite
    *(optional, string)* 匹配重写后的主机头字段。

  path_rewrite
    *(optional, string)* 匹配重写后的路径头字段。

  path_redirect
    *(optional, string)* 匹配返回的重定向路径。

  request_header_fields, response_header_fields
    *(optional, array, deprecated)*  匹配列出的头部字段。比如包括 "path"、"cookie"、
    和 "date" 字段。在所有其他测试用例之后，将检查头部字段。因此，在适用的情况下，检查的头部字段将是重定向或重写的路由的头部字段。
    这些字段已弃用。请改用 request_header_matches 和 response_header_matches。

    key
      *(required, string)* 要匹配的头部字段的名称。

    value
      *(required, string)* 要匹配的头部字段的值。

  request_header_matches, response_header_matches
    *(optional, array)*  列出的头部字段的匹配器。比如包括 "path"、"cookie"、
    和 "date" 字段，以及在输入或路由中设置的自定义标头。在所有其他测试用例之后，将检查头部字段。
    因此，在适用的情况下，检查的头部字段将是重定向或重写的路由的头部字段。
    - Matchers 被指定为 :ref:`HeaderMatchers <envoy_api_msg_route.HeaderMatcher>`，并且行为相同。

覆盖范围
--------

路由器检查工具将在测试运行成功结束时报告路由覆盖范围。

.. code:: bash

  > bazel-bin/test/tools/router_check/router_check_tool --config-path ... --test-path ...
  Current route coverage: 0.0744863

通过 `-f` 或 `--fail-under` 参数，可以利用此报告来强制执行最低覆盖率。如果覆盖率低于此百分比，则测试运行将失败。

.. code:: bash

  > bazel-bin/test/tools/router_check/router_check_tool --config-path ... --test-path ... --fail-under 8
  Current route coverage: 7.44863%
  Failed to meet coverage requirement: 8%

默认情况下，覆盖率报告通过检查至少一个字段是否经过每个路由的验证来衡量测试覆盖率。但是，那些未经验证，后来又被更改的字段会在测试中留下空白。为了获得更全面的覆盖，你可以添加 `--covall` 参数，它将考虑到所有可能测试的字段来计算覆盖率。

.. code:: bash

  > bazel-bin/test/tools/router_check/router_check_tool --config-path ... --test-path ... --f 7 --covall
  Current route coverage: 6.2948%
  Failed to meet coverage requirement: 7%
