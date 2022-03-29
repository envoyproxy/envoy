load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "rds_lib",
    srcs = [
        "rds_route_config_provider_impl.cc",
        "rds_route_config_subscription.cc",
        "route_config_provider_manager.cc",
        "route_config_update_receiver_impl.cc",
        "static_route_config_provider_impl.cc",
        "util.cc",
    ],
    hdrs = [
        "common/config_traits_impl.h",
        "common/proto_traits_impl.h",
        "common/route_config_provider_manager.h",
        "common/route_config_provider_manager_impl.h",
        "rds_route_config_provider_impl.h",
        "rds_route_config_subscription.h",
        "route_config_provider_manager.h",
        "route_config_update_receiver_impl.h",
        "static_route_config_provider_impl.h",
        "util.h",
    ],
    deps = [
        "//envoy/rds:rds_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
