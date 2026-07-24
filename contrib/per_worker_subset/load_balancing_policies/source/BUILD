load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "per_worker_subset_lb_lib",
    srcs = ["per_worker_subset_lb.cc"],
    hdrs = ["per_worker_subset_lb.h"],
    repository = "@envoy",
    visibility = [
        "//contrib/per_worker_subset/load_balancing_policies/test:__pkg__",
        "//test/extensions/load_balancing_policies/per_worker_subset:__pkg__",
    ],
    deps = [
        "//envoy/common:random_generator_interface",
        "//envoy/common:time_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/upstream:load_balancer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:upstream_lib",
        # Inner stock LBs used when host_selection_strategy is
        # ENVOY_ROUND_ROBIN or ENVOY_P2C. SimpleRoundRobin needs neither, but the
        # library always links them -- the constructor picks at most one
        # inner LB per worker at construction time.
        "//source/extensions/load_balancing_policies/round_robin:round_robin_lb_lib",
        "//source/extensions/load_balancing_policies/least_request:least_request_lb_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/common/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/least_request/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/load_balancing_policies/per_worker_subset/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":per_worker_subset_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//contrib/envoy/extensions/load_balancing_policies/per_worker_subset/v3alpha:pkg_cc_proto",
    ],
)
