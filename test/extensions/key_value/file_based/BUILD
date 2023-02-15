load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "alternate_protocols_cache_impl_test",
    srcs = ["alternate_protocols_cache_impl_test.cc"],
    deps = [
        "//source/common/common:key_value_store_lib",
        "//source/common/http:http_server_properties_cache",
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/common/http:common_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/common/key_value/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/key_value/file_based/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "key_value_store_test",
    srcs = ["key_value_store_test.cc"],
    deps = [
        "//source/common/common:key_value_store_lib",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:file_system_for_test_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)
