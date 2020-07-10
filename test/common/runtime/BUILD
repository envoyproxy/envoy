load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(["filesystem_setup.sh"])

filegroup(
    name = "filesystem_test_data",
    srcs = glob(["test_data/**"]),
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = [
        "utility.h",
    ],
    deps = [
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_test(
    name = "runtime_protos_test",
    srcs = ["runtime_protos_test.cc"],
    deps = [
        "//source/common/runtime:runtime_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "runtime_impl_test",
    srcs = ["runtime_impl_test.cc"],
    data = glob(["test_data/**"]) + ["filesystem_setup.sh"],
    # Fails on windows with cr/lf yaml file checkouts
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/config:runtime_utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/runtime/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "runtime_flag_override_test",
    srcs = ["runtime_flag_override_test.cc"],
    args = [
        "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false",
    ],
    coverage = False,
    deps = [
        "//source/common/runtime:runtime_lib",
    ],
)
