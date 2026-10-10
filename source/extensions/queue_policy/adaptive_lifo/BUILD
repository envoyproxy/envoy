load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "adaptive_lifo_queue_policy_impl",
    srcs = ["adaptive_lifo_queue_policy.cc"],
    hdrs = ["adaptive_lifo_queue_policy.h"],
    deps = [
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/conn_pool:conn_pool_base_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/queue_policy:queue_policy_base_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status:statusor",
        "@envoy_api//envoy/extensions/queue_policy/adaptive_lifo/v3:pkg_cc_proto",
    ],
)
