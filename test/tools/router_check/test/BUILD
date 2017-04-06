package(default_visibility = ["//visibility:public"])

sh_test(
    name = "router_tool_test",
    srcs = ["route_tests.sh"],
    data = [
        ":configs",
        "//test/tools/router_check:router_check_tool",
    ],
)

filegroup(
    name = "configs",
    srcs = glob(["config/*.json"]),
)
