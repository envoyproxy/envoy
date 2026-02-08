load("//test/extensions/dynamic_modules/test_data/rust:test_data.bzl", "test_program")

licenses(["notice"])  # Apache 2

package(default_visibility = [
    "//test/extensions/access_loggers/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules/bootstrap:__pkg__",
    "//test/extensions/dynamic_modules/http:__pkg__",
    "//test/extensions/dynamic_modules/network:__pkg__",
])

test_program(name = "no_op")

test_program(name = "network_no_op")

test_program(name = "no_program_init")

test_program(name = "program_init_fail")

test_program(name = "abi_version_mismatch")

test_program(name = "http")

test_program(name = "http_integration_test")

test_program(name = "http_stream_callouts_test")

test_program(name = "bootstrap_integration_test")

test_program(name = "bootstrap_stats_test")

test_program(name = "bootstrap_function_registry_test")

test_program(name = "access_log_integration_test")
