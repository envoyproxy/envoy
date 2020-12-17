.. _install_tools_route_table_check_tool:

路由表检查工具
=======================

路由表检查工具用来检查经过路由器匹配返回的路由参数值是否符合预期。
本工具还可以用来检查路径重定向、路径重写、或主机重写是否符合预期。

用法
  router_check_tool [-t <string>] [-c <string>] [-d] [-p] [--] [--version] [-h] <unlabelledConfigStrings>
    -t <string>,  --test-path <string>
      工具的 JSON 格式配置文件路径。可以在
      :ref:`config <config_tools_router_check_tool>` 中找到工具的 JSON 格式配置文件 schema。
      工具的配置文件中指定一些网址（包含权限和路径）及其预期的路由参数值。额外的参数是可选的，例如：额外的头信息。
      
      Schema：本工具所有的内部 schema 均基于 :repo:`proto3 <test/tools/router_check/validation.proto>`。

    -c <string>,  --config-path <string>
      v2 路由器配置文件路径（YAML 或 JSON）。可以在 :ref:`config <envoy_api_file_envoy/api/v2/route/route.proto>`
      中找到路由器的配置文件 schema，文件的扩展名必须反应其文件格式类型（例如，.json 代表 JSON 格式 .yaml 代表 YAML 格式）。

    -d,  --details
      展示详细的测试执行结果。首行表示测试的名称。

    --only-show-failures
      只展示失败测试的测试结果。如果设定了 details 标志，则省略通过测试的测试名称。

    -f, --fail-under
      表示路由测试覆盖率的百分比值，低于该值时测试将会失败。

    --covall
      启用全面的代码覆盖率百分比计算，同时考虑所有可能的断言。展示缺失的测试。

    --disable-deprecation-check
      禁用 RouteConfiguration proto 的弃用检查项。

    -h,  --help
      展示用法信息并退出。

输出
  当测试用例结果与期望的路由参数值不符时，程序将会以状态值 EXIT_FAILURE 退出。

  当测试失败时，如果设定了 ``--details`` 标志，失败测试用例的详细信息将会被打印出来。
  第一个字段是预期的路由参数值。第二个字段是实际的路由参数值。第三个字段表示对比的参数名称。
  下例中，除 Test_2 和 Test_5 失败以外，其余的测试都通过了。在失败的测试用例中，冲突细节将被打印出来。::

    Test_1
    Test_2
    default other virtual_host_name
    Test_3
    Test_4
    Test_5
    locations ats cluster_name
    Test_6

构建
  本工具可以通过 Bazel 本地构建。::

    bazel build //test/tools/router_check:router_check_tool

运行
  示例 ::

    bazel-bin/test/tools/router_check/router_check_tool -c router_config.(yaml|json) -t tool_config.json --details

测试
  可以通过 bazel 运行一个 bash shell 脚本进行测试。测试中通过使用不同的路由器和配置文件进行路由对比。配置文件可以在
  test/tools/router_check/test/config/... 下找到。::

    bazel test //test/tools/router_check/...
