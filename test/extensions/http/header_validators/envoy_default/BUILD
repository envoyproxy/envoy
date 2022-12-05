load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "header_validator_test",
    srcs = [
        "header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_test_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//source/extensions/http/header_validators/envoy_default:error_codes",
        "//source/extensions/http/header_validators/envoy_default:header_validator_common",
    ],
)

envoy_extension_cc_test(
    name = "http1_header_validator_test",
    srcs = [
        "http1_header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_test_lib",
        "//source/extensions/http/header_validators/envoy_default:error_codes",
        "//source/extensions/http/header_validators/envoy_default:http1_header_validator",
    ],
)

envoy_extension_cc_test(
    name = "http2_header_validator_test",
    srcs = [
        "http2_header_validator_test.cc",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        ":header_validator_test_lib",
        "//source/extensions/http/header_validators/envoy_default:character_tables",
        "//source/extensions/http/header_validators/envoy_default:error_codes",
        "//source/extensions/http/header_validators/envoy_default:http2_header_validator",
    ],
)

envoy_extension_cc_test_library(
    name = "header_validator_test_lib",
    hdrs = [
        "header_validator_test.h",
    ],
    extension_names = ["envoy.http.header_validators.envoy_default"],
    deps = [
        "//source/extensions/http/header_validators/envoy_default:config",
        "//test/mocks/stream_info:stream_info_mocks",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
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
        "//test/mocks/server:factory_context_mocks",
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
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)
