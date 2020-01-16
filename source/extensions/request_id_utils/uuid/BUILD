licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "uuid_lib",
    srcs = [
        "uuid_impl.cc",
    ],
    hdrs = [
        "uuid_impl.h",
    ],
    external_deps = ["abseil_base"],
    deps = [
        "//include/envoy/request_id_utils:request_id_utils_interface",
        "//source/common/common:utility_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":uuid_lib",
        "//include/envoy/registry",
        "//include/envoy/server:request_id_utils_config_interface",
        "//source/common/protobuf",
        "//source/extensions/request_id_utils:well_known_names",
    ],
)
