load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "header_validator_common",
    srcs = [
        "header_validator.cc",
    ],
    hdrs = [
        "header_validator.h",
    ],
    visibility = [
        "//test/common/http/http1:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        ":character_tables",
        ":path_normalizer",
        "//envoy/http:header_validator_errors",
        "//envoy/http:header_validator_interface",
        "//source/common/http:headers_lib",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "character_tables",
    hdrs = [
        "character_tables.h",
    ],
    visibility = [
        "//test/common/http/http1:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
        "//test/integration:__subpackages__",
    ],
)

envoy_cc_library(
    name = "config_overrides",
    hdrs = [
        "config_overrides.h",
    ],
    visibility = [
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "path_normalizer",
    srcs = [
        "path_normalizer.cc",
    ],
    hdrs = [
        "path_normalizer.h",
    ],
    visibility = [
        "//test/common/http/http1:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        ":character_tables",
        ":config_overrides",
        "//envoy/http:header_validator_errors",
        "//envoy/http:header_validator_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/runtime:runtime_features_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http1_header_validator",
    srcs = [
        "http1_header_validator.cc",
    ],
    hdrs = [
        "http1_header_validator.h",
    ],
    visibility = [
        "//test/common/http:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        ":header_validator_common",
        "//envoy/http:header_validator_errors",
        "//envoy/http:header_validator_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/functional:bind_front",
    ],
)

envoy_cc_library(
    name = "http2_header_validator",
    srcs = [
        "http2_header_validator.cc",
    ],
    hdrs = [
        "http2_header_validator.h",
    ],
    visibility = [
        "//test/common/http/http2:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        ":header_validator_common",
        "//envoy/http:header_validator_errors",
        "//envoy/http:header_validator_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/functional:bind_front",
    ],
)

envoy_cc_library(
    name = "header_validator_factory",
    srcs = [
        "header_validator_factory.cc",
    ],
    hdrs = [
        "header_validator_factory.h",
    ],
    visibility = [
        "//test/common/http/http2:__subpackages__",
        "//test/extensions/http/header_validators/envoy_default:__subpackages__",
    ],
    deps = [
        ":http1_header_validator",
        ":http2_header_validator",
        "//envoy/http:header_validator_interface",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        "//source/server/admin:__subpackages__",
    ],
    deps = [
        ":header_validator_factory",
        "//envoy/http:header_validator_factory_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)
