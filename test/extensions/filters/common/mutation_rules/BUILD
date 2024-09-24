load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "mutation_rules_test",
    srcs = ["mutation_rules_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/common/mutation_rules:mutation_rules_lib",
    ],
)
