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
    name = "connect_handler_test",
    srcs = ["connect_handler_test.cc"],
    extension_names = ["envoy.filters.listener.connect_handler"],
    #TODO(davinci26): The test passes on Windows *but* http inspector
    # *used* to rely on Event::FileTriggerType::Edge and we got away with it
    # because we mock the dispatcher. Need to verify that the scenario is
    # actually working.
    deps = [
        "//source/common/common:hex_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/listener/connect_handler:connect_handler_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_extension_cc_test(
    name = "connect_handler_config_test",
    srcs = ["connect_handler_config_test.cc"],
    extension_names = ["envoy.filters.listener.connect_handler"],
    deps = [
        "//source/extensions/filters/listener/connect_handler:config",
        "//source/extensions/filters/listener/connect_handler:connect_handler_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "connect_handler_fuzz_test",
    srcs = ["connect_handler_fuzz_test.cc"],
    corpus = "connect_handler_corpus",
    deps = [
        "//source/extensions/filters/listener/connect_handler:connect_handler_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)
