load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "default_attribute_builder_lib",
    srcs = [
        "default_attribute_builder.cc",
        "default_attribute_builder_factory.cc",
    ],
    hdrs = [
        "default_attribute_builder.h",
        "default_attribute_builder_factory.h",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/filters/http/ext_proc:attribute_builder_interface",
        "//source/extensions/filters/http/ext_proc:matching_utils_lib",
        "@com_google_absl//absl/strings:string_view",
        "@envoy_api//envoy/extensions/http/ext_proc/attribute_builders/default_attribute_builder/v3:pkg_cc_proto",
    ],
)
