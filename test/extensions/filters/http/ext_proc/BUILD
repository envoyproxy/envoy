load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
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
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/ext_proc:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    size = "small",
    srcs = ["filter_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":mock_server_lib",
        ":utils_lib",
        "//envoy/http:filter_interface",
        "//envoy/network:filter_interface",
        "//source/common/http:context_lib",
        "//source/common/http:rds_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/http/ext_proc",
        "//test/common/http:common_lib",
        "//test/common/http:conn_manager_impl_test_base_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:stream_encoder_mock",
        "//test/mocks/http:stream_mock",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/proto:helloworld_proto_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "state_test",
    size = "small",
    srcs = ["state_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/ext_proc",
    ],
)

envoy_extension_cc_test(
    name = "ordering_test",
    size = "small",
    srcs = ["ordering_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":mock_server_lib",
        "//source/extensions/filters/http/ext_proc",
        "//test/common/http:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "client_test",
    size = "small",
    srcs = ["client_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/ext_proc:client_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "matching_utils_test",
    size = "small",
    srcs = ["matching_utils_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":utils_lib",
        "//source/extensions/filters/http/ext_proc:matching_utils_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "mutation_utils_test",
    size = "small",
    srcs = ["mutation_utils_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":utils_lib",
        "//source/extensions/filters/common/mutation_rules:mutation_rules_lib",
        "//source/extensions/filters/http/ext_proc:mutation_utils_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/common/mutation_rules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "ext_proc_integration_test",
    size = "large",  # This test can take a while under tsan.
    srcs = ["ext_proc_integration_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = [
        "envoy.filters.http.ext_proc",
        # TODO(jbohanon) use a test filter here instead of production filter
        "envoy.filters.http.set_metadata",
    ],
    rbe_pool = "2core",
    shard_count = 8,
    tags = [
        "cpu:3",
        "skip_on_windows",
    ],
    deps = [
        ":logging_test_filter_lib",
        ":tracer_test_filter_lib",
        ":utils_lib",
        "//source/extensions/filters/http/ext_proc:config",
        "//source/extensions/filters/http/set_metadata:config",
        "//source/extensions/retry/host/previous_hosts:config",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:stream_info_to_headers_filter_lib",
        "//test/proto:helloworld_proto_cc_proto",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/set_metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/upstream_codec/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
        "@ocp//ocpdiag/core/testing:status_matchers",
    ],
)

envoy_extension_cc_test(
    name = "streaming_integration_test",
    size = "large",
    srcs = ["streaming_integration_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
        "skip_on_windows",
    ],
    deps = [
        ":test_processor_lib",
        ":utils_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "test_processor_lib",
    srcs = ["test_processor.cc"],
    hdrs = ["test_processor.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/network:address_interface",
        "//test/test_common:network_utility_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_grpc",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "mock_server_lib",
    srcs = ["mock_server.cc"],
    hdrs = ["mock_server.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/ext_proc:client_interface",
    ],
)

envoy_extension_cc_test_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/http:header_map_interface",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings:str_format",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test_library(
    name = "ext_proc_grpc_fuzz_lib",
    srcs = ["ext_proc_grpc_fuzz_helper.cc"],
    hdrs = ["ext_proc_grpc_fuzz_helper.h"],
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/grpc:common_lib",
        "//test/common/http:common_lib",
        "//test/fuzz:fuzz_runner_lib",
        "//test/fuzz:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "ext_proc_grpc_fuzz_proto",
    srcs = ["ext_proc_grpc_fuzz.proto"],
)

EXT_PROC_GRPC_FUZZ_TEST_DEPS = [
    ":ext_proc_grpc_fuzz_lib",
    ":ext_proc_grpc_fuzz_proto_cc_proto",
    ":test_processor_lib",
    "//source/common/network:address_lib",
    "//source/extensions/filters/http/ext_proc:config",
    "//test/common/http:common_lib",
    "//test/fuzz:utility_lib",
    "//test/integration:http_integration_lib",
    "//test/test_common:utility_lib",
    "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
    "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    "@envoy_api//envoy/type/v3:pkg_cc_proto",
]

envoy_cc_fuzz_test(
    name = "ext_proc_grpc_fuzz_test",
    srcs = ["ext_proc_grpc_fuzz.cc"],
    hdrs = ["ext_proc_grpc_fuzz.h"],
    corpus = "ext_proc_grpc_corpus",
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = EXT_PROC_GRPC_FUZZ_TEST_DEPS,
)

envoy_cc_fuzz_test(
    name = "ext_proc_grpc_persistent_fuzz_test",
    srcs = ["ext_proc_grpc_fuzz_persistent.cc"],
    hdrs = ["ext_proc_grpc_fuzz.h"],
    corpus = "ext_proc_grpc_corpus",
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = EXT_PROC_GRPC_FUZZ_TEST_DEPS,
)

envoy_extension_cc_test(
    name = "ext_proc_benchmark_test",
    srcs = ["ext_proc_benchmark_test.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":test_processor_lib",
        "//envoy/http:header_map_interface",
        "//source/common/common:perf_annotation_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/http/ext_proc:config",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/ext_proc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "logging_test_filter_proto",
    srcs = ["logging_test_filter.proto"],
)

envoy_extension_cc_test_library(
    name = "logging_test_filter_lib",
    srcs = [
        "logging_test_filter.cc",
    ],
    extension_names = ["envoy.filters.http.ext_proc"],
    tags = ["skip_on_windows"],
    deps = [
        ":logging_test_filter_proto_cc_proto",
        "//envoy/http:filter_interface",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/ext_proc",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "tracer_test_filter_proto",
    srcs = [":tracer_test_filter.proto"],
)

envoy_extension_cc_test_library(
    name = "tracer_test_filter_lib",
    srcs = ["tracer_test_filter.cc"],
    extension_names = ["envoy.filters.http.ext_proc"],
    deps = [
        ":tracer_test_filter_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/tracers/common:factory_base_lib",
    ],
)
