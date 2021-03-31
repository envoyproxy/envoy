load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "shuffle_shard_lb",
    srcs = [
        "load_balancer.cc",
        "load_balancer.h",
    ],
    hdrs = [
      "load_balancer.h",
    ],
    deps = [
      "//source/common/config:well_known_names",
      "//source/common/upstream:load_balancer_lib",
      "@envoy_api//envoy/extensions/load_balancers/shuffle_shard/v3:pkg_cc_proto",
      "@com_github_cgetzen_cpp_infima//:infima",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = [
        "config.h",
    ],
    category = "envoy.bootstrap",
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "shuffle_shard_lb",
        "//include/envoy/upstream:upstream_interface",
        "//include/envoy/registry",
        "//include/envoy/server:bootstrap_extension_config_interface",
        "//include/envoy/server:factory_context_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/common/wasm:wasm_lib",
        "//source/extensions/grpc_credentials:well_known_names",
        "@envoy_api//envoy/extensions/wasm/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancers/shuffle_shard/v3:pkg_cc_proto",
    ],
)
