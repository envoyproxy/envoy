licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

sh_test(
    name = "envoy_static_test",
    srcs = ["envoy_static_test.sh"],
    data = ["//source/exe:envoy-static"],
    # NOTE: In some environments, ASAN causes dynamic linking no matter what, so don't run this
    # test when doing ASAN.
    tags = ["no_asan"],
)

sh_test(
    name = "version_out_test",
    srcs = ["version_out_test.sh"],
    data = ["//source/exe:envoy-static"],
)

envoy_cc_test(
    name = "main_common_test",
    srcs = ["main_common_test.cc"],
    data = ["//test/config/integration:google_com_proxy_port_0"],
    deps = [
        "//source/exe:envoy_main_common_lib",
        "//test/test_common:contention_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "signals_test",
    srcs = ["signals_test.cc"],
    tags = ["backtrace"],
    deps = [
        "//source/exe:sigaction_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "terminate_handler_test",
    srcs = ["terminate_handler_test.cc"],
    tags = ["backtrace"],
    deps = [
        "//source/exe:terminate_handler_lib",
        "//test/test_common:utility_lib",
    ],
)
