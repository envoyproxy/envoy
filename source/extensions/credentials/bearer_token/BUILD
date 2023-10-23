load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "bearer_token_lib",
    srcs = ["bearer_token_impl.cc"],
    hdrs = ["bearer_token_impl.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/credentials/common:credential_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":bearer_token_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/credentials/common:credential_factory_base_lib",
        "@envoy_api//envoy/extensions/credentials/bearer_token/v3:pkg_cc_proto",
    ],
)
