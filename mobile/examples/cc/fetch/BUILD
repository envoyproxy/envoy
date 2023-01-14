load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "fetch_lib",
    srcs = [
	"fetch.cc",
    ],
    hdrs = [
	"fetch.h",
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
    name = "fetch",
    srcs = [ "fetch_main.cc" ],
    deps = [ ":fetch_lib",

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