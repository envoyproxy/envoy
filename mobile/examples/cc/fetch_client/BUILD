load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "fetch_client_lib",
    srcs = [
        "fetch_client.cc",
    ],
    hdrs = [
        "fetch_client.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/http:header_map_interface",
        "@envoy//source/common/http:header_map_lib",
    ],
)

cc_binary(
    name = "fetch_client",
    srcs = ["fetch_client_main.cc"],
    deps = [
        ":fetch_client_lib",
        "@envoy//source/common/api:api_lib",
        "@envoy//source/common/common:random_generator_lib",
        "@envoy//source/common/common:thread_lib",
        "@envoy//source/common/event:real_time_system_lib",
        "@envoy//source/common/stats:allocator_lib",
        "@envoy//source/common/stats:thread_local_store_lib",
        "@envoy//source/exe:platform_header_lib",
        "@envoy//source/exe:platform_impl_lib",
        "@envoy//source/exe:process_wide_lib",
    ],
)

cc_test(
    name = "fetch_client_test",
    srcs = ["fetch_client_test.cc"],
    tags = [
        "requires-net:external",
        "requires-net:ipv4",
    ],
    deps = [
        ":fetch_client_lib",
        "//testing/base/public:gunit_main",
        "//third_party/envoy/src/mobile/envoy_build_config:extension_registry",
        "//third_party/envoy/src/mobile/library/cc:envoy_engine_cc_lib_no_stamp",
        "//third_party/envoy/src/source/common/api:api_lib",
        "//third_party/envoy/src/source/common/common:random_generator_lib",
        "//third_party/envoy/src/source/common/common:thread_lib",
        "//third_party/envoy/src/source/common/event:real_time_system_lib",
        "//third_party/envoy/src/source/common/stats:allocator_lib",
        "//third_party/envoy/src/source/common/stats:thread_local_store_lib",
        "//third_party/envoy/src/source/exe:platform_header_lib",
        "//third_party/envoy/src/source/exe:platform_impl_lib",
        "//third_party/envoy/src/source/exe:process_wide_lib",
    ],
)
