load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "save_processing_response_lib",
    srcs = [
        "save_processing_response.cc",
        "save_processing_response_factory.cc",
    ],
    hdrs = [
        "save_processing_response.h",
        "save_processing_response_factory.h",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/http/ext_proc:on_processing_response_interface",
        "@abseil-cpp//absl/strings:string_view",
        "@envoy_api//envoy/extensions/http/ext_proc/response_processors/save_processing_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)
