load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "runtime_keys_lib",
    srcs = [
        "runtime_keys.cc",
    ],
    hdrs = [
        "runtime_keys.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "runtime_features_lib",
    srcs = [
        "runtime_features.cc",
    ],
    hdrs = [
        "runtime_features.h",
    ],
    # ABSL_FLAGS_STRIP_NAMES controls the behavior of the ABSL_FLAG
    # macro. By default, it is defined on mobile platforms and false elsewhere.
    # When defined, it causes flags to be registered in the global flag registry,
    # which is required for Envoy runtime flags to work. By forcing this to be
    # defined in source/common/runtime:runtime_features_lib, we ensure that
    # even on mobile, runtime features work correctly since the ABSL_FLAGS
    # which control them live in that target. This does NOT cause ABSL_FLAGS
    # defined in other targets to be added to the registry, but that does not
    # affect Envoy's usage.
    copts = [
        "-DABSL_FLAGS_STRIP_NAMES=0",
    ],
    deps = [
        # AVOID ADDING TO THESE DEPENDENCIES IF POSSIBLE
        # Any code using runtime guards depends on this library, and the more dependencies there are,
        # the harder it is to runtime-guard without dependency loops.
        "@com_google_absl//absl/flags:commandlineflag",
        "@com_google_absl//absl/flags:flag",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:hash_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "runtime_protos_lib",
    hdrs = [
        "runtime_protos.h",
    ],
    deps = [
        "//envoy/runtime:runtime_interface",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "runtime_lib",
    srcs = [
        "runtime_impl.cc",
    ],
    hdrs = [
        "runtime_impl.h",
    ],
    deps = [
        ":runtime_features_lib",
        ":runtime_protos_lib",
        "//envoy/config:subscription_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/init:manager_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/filesystem:directory_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/init:watcher_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic/platform:quiche_flags_impl_lib",
    ]),
)
