load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "hessian2_utils_lib",
    srcs = ["hessian2_utils.cc"],
    hdrs = ["hessian2_utils.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/singleton:const_singleton",
        "@com_github_alibaba_hessian2_codec//hessian2:codec_impl_lib",
        "@com_github_alibaba_hessian2_codec//hessian2/basic_codec:object_codec_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "hessian2_serializer_lib",
        ":hessian2_utils_lib",
        ":message_lib",
        ":metadata_lib",
        ":serializer_interface",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    deps = [
        ":message_lib",
        "//source/common/buffer:buffer_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "message_lib",
    srcs = [
        "message.cc",
    ],
    hdrs = [
        "message.h",
    ],
    deps = [
        ":hessian2_utils_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "serializer_interface",
    hdrs = [
        "serializer.h",
    ],
    deps = [
        ":hessian2_utils_lib",
        ":message_lib",
        ":metadata_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "hessian2_serializer_lib",
    srcs = [
        "hessian2_serializer_impl.cc",
    ],
    hdrs = [
        "hessian2_serializer_impl.h",
    ],
    deps = [
        ":hessian2_utils_lib",
        ":message_lib",
        ":metadata_lib",
        ":serializer_interface",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
    ],
)
