load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "http_inspector_test",
    srcs = ["http_inspector_test.cc"],
    extension_name = "envoy.filters.listener.http_inspector",
    #TODO(davinci26): The test passes on Windows *but* http inspector
    # *used* to rely on Event::FileTriggerType::Edge and we got away with it
    # because we mock the dispatcher. Need to verify that the scenario is
    # actually working.
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_inspector_config_test",
    srcs = ["http_inspector_config_test.cc"],
    extension_name = "envoy.filters.listener.http_inspector",
    deps = [
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/listener/http_inspector:config",
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "http_inspector_fuzz_test",
    srcs = ["http_inspector_fuzz_test.cc"],
    corpus = "http_inspector_corpus",
    deps = [
        "//source/extensions/filters/listener/http_inspector:http_inspector_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)
