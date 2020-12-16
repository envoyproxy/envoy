.. _install_tools_config_load_check_tool:

配置加载检查工具
======================


配置加载检查工具检查 JSON 格式的配置文件是否有效，并且需要符合 Envoy JSON schema。该工具引用``test/config_test/config_test.cc``
测试加载 JSON 配置文件并使用它运行服务器初始化配置。

输入
  该工具需要指向一个包含 Envoy JSON 格式的配置文件路径，该工具将递归地遍历文件系统树，并对找到的每个文件运行配置测试。
  请记住，该工具将尝试加载路径中找到的所有文件。

输出
  该工具使用当前测试的配置或初始化服务器配置时输出 Envoy 日志。如果存在错误格式的 JSON 文件
  或不符合 Envoy JSON 架构的配置文件，工具将退出并返回 exit_FAILURE 状态码。如果该工具成功加载
  所有找到的配置文件，它将退出并返回 EXIT_SUCCESS 状态码。

构建
  该工具可以在本地使用 Bazel 构建。 ::

    bazel build //test/tools/config_load_check:config_load_check_tool

运行
  该工具使用下述路径。::

    bazel-bin/test/tools/config_load_check/config_load_check_tool PATH
