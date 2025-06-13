load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "redis_http_cache_lookup_test",
    srcs = ["redis_http_cache_lookup_test.cc"],
    extension_names = ["envoy.extensions.http.cache.redis_http_cache"],
    deps = [
        "//source/extensions/http/cache/redis_http_cache:config",
         "//test/mocks/event:event_mocks",
         "//test/mocks/upstream:upstream_mocks",
         "//test/mocks/thread_local:thread_local_mocks",
         "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "redis_http_cache_insert_test",
    srcs = ["redis_http_cache_insert_test.cc"],
    extension_names = ["envoy.extensions.http.cache.redis_http_cache"],
    deps = [
    ],
)

