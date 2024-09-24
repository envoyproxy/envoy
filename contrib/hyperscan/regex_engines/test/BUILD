load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/hyperscan/regex_engines/source:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "regex_test",
    srcs = ["regex_test.cc"],
    deps = [
        "//contrib/hyperscan/regex_engines/source:regex_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/hyperscan/regex_engines/source:config",
        "//test/integration:integration_lib",
    ],
)
