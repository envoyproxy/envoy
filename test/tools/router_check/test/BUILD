package(default_visibility = ["//visibility:public"])

sh_test(
    name = "router_tool_test",
    srcs = ["route_tests.sh"],
    args = [
        "$(location //test/tools/router_check:router_check_tool)",
    ],
    data = [
        ":configs",
        "//test/tools/router_check:router_check_tool",
    ],
)

filegroup(
    name = "configs",
    srcs = glob(["config/*.json"]),
)
