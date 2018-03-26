licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/common/access_log:access_log_lib",
        "//source/common/config:well_known_names",
        "//source/common/dynamo:dynamo_filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/client_ssl_auth:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/server/config/access_log:file_access_log_lib",
        "//source/server/config/network:ext_authz_lib",
        "//source/server/config/network:http_connection_manager_lib",
        "//source/server/config/network:mongo_proxy_lib",
        "//source/server/config/network:ratelimit_lib",
        "//source/server/config/network:redis_proxy_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "mongo_proxy_test",
    srcs = ["mongo_proxy_test.cc"],
    deps = [
        "//source/server/config/network:mongo_proxy_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http_connection_manager_test",
    srcs = ["http_connection_manager_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/router:rds_lib",
        "//source/server/config/http:dynamo_lib",
        "//source/server/config/network:http_connection_manager_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
