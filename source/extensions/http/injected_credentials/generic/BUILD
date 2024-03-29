load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "generic_lib",
    srcs = ["generic_impl.cc"],
    hdrs = ["generic_impl.h"],
    deps = [
        "//source/extensions/http/injected_credentials/common:credential_interface",
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":generic_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/http/injected_credentials/common:credential_factory_base_lib",
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
        "@envoy_api//envoy/extensions/http/injected_credentials/generic/v3:pkg_cc_proto",
    ],
)
