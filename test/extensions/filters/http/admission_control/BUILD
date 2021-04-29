load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "admission_control_filter_test",
    srcs = ["admission_control_filter_test.cc"],
    extension_name = "envoy.filters.http.admission_control",
    deps = [
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/admission_control:admission_control_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.admission_control",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/admission_control:admission_control_filter_lib",
        "//source/extensions/filters/http/admission_control:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "success_criteria_evaluator_test",
    srcs = ["success_criteria_evaluator_test.cc"],
    extension_name = "envoy.filters.http.admission_control",
    deps = [
        "//source/extensions/filters/http/admission_control:admission_control_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "admission_control_integration_test",
    srcs = ["admission_control_integration_test.cc"],
    extension_name = "envoy.filters.http.admission_control",
    deps = [
        "//source/extensions/filters/http/admission_control:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "admission_controller_test",
    srcs = ["controller_test.cc"],
    extension_name = "envoy.filters.http.admission_control",
    deps = [
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/admission_control:admission_control_filter_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/admission_control/v3alpha:pkg_cc_proto",
    ],
)
