load("//test/extensions/dynamic_modules/test_data/c:test_data.bzl", "test_program")

licenses(["notice"])  # Apache 2

package(default_visibility = [
    "//test/extensions/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules/http:__pkg__",
])

test_program(name = "no_op")

test_program(name = "no_program_init")

test_program(name = "program_init_assert")

test_program(name = "program_init_fail")

test_program(name = "abi_version_mismatch")

test_program(name = "no_http_config_new")

test_program(name = "no_http_config_destroy")

test_program(name = "no_http_filter_new")

test_program(name = "no_http_filter_destroy")

test_program(name = "no_http_filter_request_headers")

test_program(name = "no_http_filter_request_body")

test_program(name = "no_http_filter_request_trailers")

test_program(name = "no_http_filter_response_headers")

test_program(name = "no_http_filter_response_body")

test_program(name = "no_http_filter_response_trailers")
