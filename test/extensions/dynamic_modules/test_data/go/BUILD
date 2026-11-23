load("//test/extensions/dynamic_modules/test_data/go:test_data.bzl", "test_program")

licenses(["notice"])  # Apache 2

# Each module provides one hook for the other extension kind. Linking catches a duplicate
# export, and the integration tests exercise the SDK hooks that remain enabled.
test_program(
    name = "bootstrap_integration_test",
    extra_srcs = ["bootstrap_integration_test/custom_cluster.go"],
    gotags = ["envoy_dynamic_modules_go_custom_cluster"],
)

test_program(
    name = "cluster_integration_test",
    extra_srcs = ["cluster_integration_test/custom_bootstrap.go"],
    gotags = ["envoy_dynamic_modules_go_custom_bootstrap"],
)

test_program(name = "early_header_mutation_integration_test")

test_program(name = "header_formatter_integration_test")

test_program(name = "http")

test_program(name = "http_integration_test")

test_program(name = "listener_integration_test")

test_program(name = "network_integration_test")

test_program(name = "stat_sink_integration_test")

test_program(name = "udp_listener_integration_test")
