load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "rendezvous_hash_lb_lib",
    srcs = ["rendezvous_hash_lb.cc"],
    hdrs = ["rendezvous_hash_lb.h"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/extensions/load_balancing_policies/common:thread_aware_lb_lib",
        "@com_google_absl//absl/numeric:bits",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/rendezvous_hash/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":rendezvous_hash_lb_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/extensions/load_balancing_policies/rendezvous_hash/v3:pkg_cc_proto",
    ],
)
