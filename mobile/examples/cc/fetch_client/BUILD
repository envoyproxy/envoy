load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_mobile_package",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

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
        "@com_google_absl//absl/synchronization",
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
