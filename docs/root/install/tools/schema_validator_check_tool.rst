.. _install_tools_schema_validator_check_tool:

Schema 验证器检查工具
=======================

Schema 验证器检查工具验证传入的配置是否符合给定的 schema。配置可以是 JSON 或者 YAML。为了验证全部的配置，可以参考 :ref:`配置加载检查工具 <install_tools_config_load_check_tool>`。

输入
  这个工具共有两个输入：

  1. 传入配置的 schema 类型会被检查。支持的类型有：

    * `route` - 用于 :ref:`路由配置 <envoy_v3_api_msg_config.route.v3.RouteConfiguration>` 验证。
    * `discovery_response` - 用于 :ref:`发现响应 <envoy_v3_api_msg_service.discovery.v3.DiscoveryResponse>` 验证。

  2. 配置文件的路径。

输出
  如果配置符合 schema，工具将以 EXIT_SUCCESS 的状态退出。如果配置不符合 schema，会输出一条错误信息，并且会详细说明不符合 schema 的内容。同时工具会以 EXIT_FAILURE 的状态退出。

构建
  这个工具可以使用 Bazel 在本地构建。 ::

    bazel build //test/tools/schema_validator:schema_validator_tool

运行中
  这个工具会使用上面描述的路径。 ::

    bazel-bin/test/tools/schema_validator/schema_validator_tool  --schema-type SCHEMA_TYPE  --config-path PATH
