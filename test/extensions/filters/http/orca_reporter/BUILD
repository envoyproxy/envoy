load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "fake_cgroup_lib",
    hdrs = ["fake_cgroup.h"],
    deps = [
        "//source/common/common:fmt_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_extension_cc_test(
    name = "cgroup_stats_test",
    srcs = ["cgroup_stats_test.cc"],
    extension_names = ["envoy.filters.http.orca_reporter"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        ":fake_cgroup_lib",
        "//source/common/common:regex_lib",
        "//source/extensions/filters/http/orca_reporter:cgroup_stats_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "orca_reporter_test",
    srcs = ["orca_reporter_test.cc"],
    extension_names = ["envoy.filters.http.orca_reporter"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        ":fake_cgroup_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:regex_lib",
        "//source/common/orca:orca_parser",
        "//source/extensions/filters/http/orca_reporter:orca_reporter_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/orca_reporter/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.orca_reporter"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        ":fake_cgroup_lib",
        "//source/extensions/filters/http/orca_reporter:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/orca_reporter/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "orca_reporter_integration_test",
    size = "large",
    srcs = ["orca_reporter_integration_test.cc"],
    extension_names = ["envoy.filters.http.orca_reporter"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        ":fake_cgroup_lib",
        "//source/common/orca:orca_parser",
        "//source/extensions/filters/http/orca_reporter:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)
