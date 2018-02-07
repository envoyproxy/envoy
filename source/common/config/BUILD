licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "address_json_lib",
    srcs = ["address_json.cc"],
    hdrs = ["address_json.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/api/v2/core:address_cc",
    ],
)

envoy_cc_library(
    name = "bootstrap_json_lib",
    srcs = ["bootstrap_json.cc"],
    hdrs = ["bootstrap_json.h"],
    deps = [
        ":address_json_lib",
        ":cds_json_lib",
        ":json_utility_lib",
        ":lds_json_lib",
        ":utility_lib",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:config_schemas_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_library(
    name = "base_json_lib",
    srcs = ["base_json.cc"],
    hdrs = ["base_json.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "cds_json_lib",
    srcs = ["cds_json.cc"],
    hdrs = ["cds_json.h"],
    deps = [
        ":address_json_lib",
        ":json_utility_lib",
        ":protocol_json_lib",
        ":tls_context_json_lib",
        ":utility_lib",
        "//include/envoy/common:optional",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/api/v2:cds_cc",
        "@envoy_api//envoy/api/v2/cluster:circuit_breaker_cc",
    ],
)

envoy_cc_library(
    name = "filesystem_subscription_lib",
    hdrs = ["filesystem_subscription_impl.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "filter_json_lib",
    srcs = ["filter_json.cc"],
    hdrs = ["filter_json.h"],
    deps = [
        ":address_json_lib",
        ":json_utility_lib",
        ":protocol_json_lib",
        ":rds_json_lib",
        ":utility_lib",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:config_schemas_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/buffer/v2:buffer_cc",
        "@envoy_api//envoy/config/filter/http/fault/v2:fault_cc",
        "@envoy_api//envoy/config/filter/http/health_check/v2:health_check_cc",
        "@envoy_api//envoy/config/filter/http/lua/v2:lua_cc",
        "@envoy_api//envoy/config/filter/http/rate_limit/v2:rate_limit_cc",
        "@envoy_api//envoy/config/filter/http/router/v2:router_cc",
        "@envoy_api//envoy/config/filter/http/squash/v2:squash_cc",
        "@envoy_api//envoy/config/filter/http/transcoder/v2:transcoder_cc",
        "@envoy_api//envoy/config/filter/network/client_ssl_auth/v2:client_ssl_auth_cc",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_cc",
        "@envoy_api//envoy/config/filter/network/mongo_proxy/v2:mongo_proxy_cc",
        "@envoy_api//envoy/config/filter/network/rate_limit/v2:rate_limit_cc",
        "@envoy_api//envoy/config/filter/network/redis_proxy/v2:redis_proxy_cc",
        "@envoy_api//envoy/config/filter/network/tcp_proxy/v2:tcp_proxy_cc",
    ],
)

envoy_cc_library(
    name = "grpc_mux_lib",
    srcs = ["grpc_mux_impl.cc"],
    hdrs = ["grpc_mux_impl.h"],
    deps = [
        ":utility_lib",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:logger_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_subscription_lib",
    hdrs = ["grpc_mux_subscription_impl.h"],
    deps = [
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/grpc:common_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:discovery_cc",
    ],
)

envoy_cc_library(
    name = "grpc_subscription_lib",
    hdrs = ["grpc_subscription_impl.h"],
    deps = [
        ":grpc_mux_lib",
        ":grpc_mux_subscription_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/grpc:async_client_interface",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "http_subscription_lib",
    hdrs = ["http_subscription_impl.h"],
    external_deps = [
        "http_api_protos",
    ],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "json_utility_lib",
    hdrs = ["json_utility.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "lds_json_lib",
    srcs = ["lds_json.cc"],
    hdrs = ["lds_json.h"],
    deps = [
        ":address_json_lib",
        ":json_utility_lib",
        ":tls_context_json_lib",
        ":utility_lib",
        ":well_known_names",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/api/v2:lds_cc",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    srcs = ["metadata.cc"],
    hdrs = ["metadata.h"],
    deps = [
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "protobuf_link_hacks",
    hdrs = ["protobuf_link_hacks.h"],
    deps = [
        "@envoy_api//envoy/service/discovery/v2:ads_cc",
        "@envoy_api//envoy/service/ratelimit/v2:rls_cc",
    ],
)

envoy_cc_library(
    name = "protocol_json_lib",
    srcs = ["protocol_json.cc"],
    hdrs = ["protocol_json.h"],
    deps = [
        ":json_utility_lib",
        "//include/envoy/json:json_object_interface",
        "@envoy_api//envoy/api/v2/core:protocol_cc",
    ],
)

envoy_cc_library(
    name = "resources_lib",
    hdrs = ["resources.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_library(
    name = "rds_json_lib",
    srcs = ["rds_json.cc"],
    hdrs = ["rds_json.h"],
    deps = [
        ":base_json_lib",
        ":json_utility_lib",
        ":metadata_lib",
        ":well_known_names",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/json:config_schemas_lib",
        "@envoy_api//envoy/api/v2:rds_cc",
    ],
)

envoy_cc_library(
    name = "subscription_factory_lib",
    hdrs = ["subscription_factory.h"],
    deps = [
        ":filesystem_subscription_lib",
        ":grpc_mux_subscription_lib",
        ":grpc_subscription_lib",
        ":http_subscription_lib",
        ":utility_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "tls_context_json_lib",
    srcs = ["tls_context_json.cc"],
    hdrs = ["tls_context_json.h"],
    deps = [
        ":json_utility_lib",
        "//include/envoy/json:json_object_interface",
        "//source/common/common:utility_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":json_utility_lib",
        ":resources_lib",
        ":well_known_names",
        "//include/envoy/config:grpc_mux_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/registry",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/grpc:common_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/stats:stats_lib",
        "@envoy_api//envoy/api/v2:cds_cc",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/api/v2:lds_cc",
        "@envoy_api//envoy/api/v2:rds_cc",
        "@envoy_api//envoy/api/v2/core:base_cc",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_cc",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    srcs = ["well_known_names.cc"],
    hdrs = ["well_known_names.h"],
    deps = ["//source/common/singleton:const_singleton"],
)
