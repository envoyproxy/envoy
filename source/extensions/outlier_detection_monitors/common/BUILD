load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "outlier_detection_monitors_lib",
    srcs = ["monitor_base_impl.cc"],
    hdrs = ["monitor_base_impl.h"],
    extra_visibility = [
        "//source/common/upstream:__subpackages__",
        "//test/common/upstream:__subpackages__",
    ],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        #"//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/outlier_detection_monitors/common/v3:pkg_cc_proto",
    ],
)
