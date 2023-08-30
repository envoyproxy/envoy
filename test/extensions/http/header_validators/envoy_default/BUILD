load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "base_header_validator_test",
    srcs = [
        "base_header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_utils_lib",
        "//envoy/http:header_validator_errors",
        "//source/common/network:utility_lib",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//source/extensions/http/header_validators/envoy_default:header_validator_common",
        "//test/mocks/http:header_validator_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_common_validation_test",
    srcs = [
        "http_common_validation_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_utils_lib",
        "//envoy/http:header_validator_errors",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//source/extensions/http/header_validators/envoy_default:http1_header_validator",
        "//source/extensions/http/header_validators/envoy_default:http2_header_validator",
        "//test/mocks/http:header_validator_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http1_header_validator_test",
    srcs = [
        "http1_header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_utils_lib",
        "//envoy/http:header_validator_errors",
        "//source/extensions/http/header_validators/envoy_default:http1_header_validator",
        "//test/mocks/http:header_validator_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http2_header_validator_test",
    srcs = [
        "http2_header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_utils_lib",
        "//envoy/http:header_validator_errors",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//source/extensions/http/header_validators/envoy_default:http2_header_validator",
        "//test/mocks/http:header_validator_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "header_validator_utils_lib",
    srcs = [
        "header_validator_utils.cc",
    ],
    hdrs = [
        "header_validator_utils.h",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        "//envoy/http:header_validator_interface",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "header_validator_factory_test",
    srcs = [
        "header_validator_factory_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/http/header_validators/envoy_default:config",
        "//test/mocks/http:header_validator_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "path_normalizer_test",
    srcs = [
        "path_normalizer_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        "//source/extensions/http/header_validators/envoy_default:path_normalizer",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        "//envoy/registry",
        "//source/extensions/http/header_validators/envoy_default:config",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "path_normalizer_fuzz_proto",
    srcs = ["path_normalizer_fuzz.proto"],
    deps = [
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "path_normalizer_fuzz_test",
    srcs = ["path_normalizer_fuzz_test.cc"],
    corpus = "path_normalizer_corpus",
    deps = [
        ":path_normalizer_fuzz_proto_cc_proto",
        "//source/extensions/http/header_validators/envoy_default:http1_header_validator",
        "//source/extensions/http/header_validators/envoy_default:path_normalizer",
        "//test/mocks/http:header_validator_mocks",
        "//test/test_common:utility_lib",
    ],
)
