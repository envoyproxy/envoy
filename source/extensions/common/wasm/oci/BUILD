load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "oci_async_datasource_lib",
    srcs = ["oci_async_datasource.cc"],
    hdrs = ["oci_async_datasource.h"],
    deps = [
        ":image_fetcher_lib",
        "//source/extensions/common/wasm:remote_async_datasource_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "image_fetcher_lib",
    srcs = [
        "oci_image_blob_fetcher.cc",
        "oci_image_manifest_fetcher.cc",
    ],
    hdrs = [
        "oci_image_blob_fetcher.h",
        "oci_image_manifest_fetcher.h",
    ],
    deps = [
        ":utility_lib",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:hex_lib",
        "//source/common/config:remote_data_fetcher_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/secret:secret_provider_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        "//source/common/json:json_loader_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)
