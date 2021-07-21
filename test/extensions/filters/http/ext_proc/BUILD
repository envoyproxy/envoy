load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        "//source/extensions/filters/http/ext_proc:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    size = "small",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":mock_server_lib",
        ":utils_lib",
        "//source/extensions/filters/http/ext_proc",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ordering_test",
    size = "small",
    srcs = ["ordering_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":mock_server_lib",
        "//source/extensions/filters/http/ext_proc",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "client_test",
    size = "small",
    srcs = ["client_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/ext_proc:client_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "mutation_utils_test",
    size = "small",
    srcs = ["mutation_utils_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":utils_lib",
        "//source/extensions/filters/http/ext_proc:mutation_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "ext_proc_integration_test",
    size = "medium",
    srcs = ["ext_proc_integration_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":utils_lib",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "streaming_integration_test",
    size = "medium",
    srcs = ["streaming_integration_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":test_processor_lib",
        ":utils_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "test_processor_lib",
    srcs = ["test_processor.cc"],
    hdrs = ["test_processor.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_grpc",
        "@envoy_api//envoy/service/ext_proc/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "mock_server_lib",
    srcs = ["mock_server.cc"],
    hdrs = ["mock_server.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        "//source/extensions/filters/http/ext_proc:client_interface",
    ],
)

envoy_extension_cc_test_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        "//envoy/http:header_map_interface",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
