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
    name = "lua_filter_test",
    srcs = ["lua_filter_test.cc"],
    extension_names = ["envoy.filters.http.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/lua:lua_filter_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "wrappers_test",
    srcs = ["wrappers_test.cc"],
    extension_names = ["envoy.filters.http.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:address_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/lua:wrappers_lib",
        "//test/extensions/filters/common/lua:lua_wrappers_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "lua_integration_test",
    size = "large",
    srcs = ["lua_integration_test.cc"],
    extension_names = ["envoy.filters.http.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/lua:config",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.lua"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/lua:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/lua/v3:pkg_cc_proto",
    ],
)
