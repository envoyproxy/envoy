load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "basic_auth_lib",
    srcs = ["basic_auth_impl.cc"],
    hdrs = ["basic_auth_impl.h"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/credentials/common:credential_interface",
        "//source/extensions/credentials/common:secret_reader_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":basic_auth_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/credentials/common:credential_factory_base_lib",
        "//source/extensions/credentials/common:secret_reader_lib",
        "@envoy_api//envoy/extensions/credentials/basic_auth/v3:pkg_cc_proto",
    ],
)
