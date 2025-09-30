load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "mapped_attribute_builder_lib",
    srcs = [
        "mapped_attribute_builder.cc",
        "mapped_attribute_builder_factory.cc",
    ],
    hdrs = [
        "mapped_attribute_builder.h",
        "mapped_attribute_builder_factory.h",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/ext_proc:matching_utils_lib",
        "//source/extensions/filters/http/ext_proc:processing_request_modifier_interface",
        "@com_google_absl//absl/strings:string_view",
        "@envoy_api//envoy/extensions/http/ext_proc/processing_request_modifiers/mapped_attribute_builder/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/ext_proc/v3:pkg_cc_proto",
    ],
)
