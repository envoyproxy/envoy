load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_binary",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_extension_package",
    "envoy_select_enable_exceptions",
    "envoy_select_hot_restart",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "select_enable_exceptions_dep",
    srcs = ["select_enable_exceptions_dep.cc"],
    hdrs = ["select_enable_exceptions_dep.h"],
)

envoy_cc_library(
    name = "select_hot_restart_dep",
    srcs = ["select_hot_restart_dep.cc"],
    hdrs = ["select_hot_restart_dep.h"],
)

envoy_cc_library(
    name = "external_lib",
    srcs = ["external_lib.cc"],
    hdrs = ["external_lib.h"],
    deps = envoy_select_enable_exceptions(
        [":select_enable_exceptions_dep"],
    ) + envoy_select_hot_restart(
        [":select_hot_restart_dep"],
    ),
)

envoy_cc_extension(
    name = "external_extension",
    srcs = ["external_extension.cc"],
    hdrs = ["external_extension.h"],
    deps = [":external_lib"],
)

envoy_cc_binary(
    name = "external_binary",
    srcs = ["external_main.cc"],
    deps = [":external_lib"],
)

envoy_cc_test_library(
    name = "external_test_lib",
    srcs = ["external_test_lib.cc"],
    hdrs = ["external_test_lib.h"],
    deps = [":external_lib"],
)

envoy_cc_mock(
    name = "external_mock",
    hdrs = ["external_mock.h"],
)

envoy_cc_test(
    name = "external_test",
    srcs = ["external_test.cc"],
    deps = [
        ":external_mock",
        ":external_test_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "external_benchmark_binary",
    srcs = ["external_benchmark.cc"],
    deps = [":external_lib"],
)

envoy_benchmark_test(
    name = "external_benchmark_test",
    benchmark_binary = "external_benchmark_binary",
)
