load("//test/extensions/dynamic_modules/test_data/c:test_data.bzl", "test_program")

licenses(["notice"])  # Apache 2

package(default_visibility = [
    "//test/extensions/access_loggers/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules:__pkg__",
    "//test/extensions/dynamic_modules/bootstrap:__pkg__",
    "//test/extensions/dynamic_modules/http:__pkg__",
    "//test/extensions/dynamic_modules/listener:__pkg__",
    "//test/extensions/dynamic_modules/network:__pkg__",
    "//test/extensions/dynamic_modules/udp:__pkg__",
])

test_program(name = "no_op")

test_program(name = "network_no_op")

test_program(name = "network_config_new_fail")

test_program(name = "network_filter_new_fail")

test_program(name = "network_stop_iteration")

test_program(name = "no_program_init")

test_program(name = "program_init_assert")

test_program(name = "program_init_fail")

test_program(name = "abi_version_mismatch")

test_program(name = "no_http_config_new")

test_program(name = "http_filter_per_route_config_new_fail")

test_program(name = "no_http_filter_per_route_config_new")

test_program(name = "no_http_filter_per_route_config_destroy")

test_program(name = "no_http_config_destroy")

test_program(name = "no_http_filter_new")

test_program(name = "no_http_filter_stream_complete")

test_program(name = "no_http_filter_destroy")

test_program(name = "no_http_filter_request_headers")

test_program(name = "no_http_filter_request_body")

test_program(name = "no_http_filter_request_trailers")

test_program(name = "no_http_filter_response_headers")

test_program(name = "no_http_filter_response_body")

test_program(name = "no_http_filter_response_trailers")

test_program(name = "program_global")

test_program(name = "program_child")

test_program(name = "listener_no_op")

test_program(name = "listener_config_new_fail")

test_program(name = "listener_filter_new_fail")

test_program(name = "listener_stop_iteration")

test_program(name = "udp_no_op")

test_program(name = "udp_stop_iteration")

test_program(name = "udp_no_config_destroy")

test_program(name = "udp_no_filter_new")

test_program(name = "udp_no_on_data")

test_program(name = "udp_no_filter_destroy")

test_program(name = "bootstrap_no_op")

test_program(name = "bootstrap_no_config_new")

test_program(name = "bootstrap_no_constructor")

test_program(name = "bootstrap_no_config_destroy")

test_program(name = "bootstrap_no_extension_new")

test_program(name = "bootstrap_no_server_initialized")

test_program(name = "bootstrap_no_worker_initialized")

test_program(name = "bootstrap_no_extension_destroy")

test_program(name = "bootstrap_extension_new_null")

test_program(name = "bootstrap_no_config_scheduled")

test_program(name = "bootstrap_no_http_callout_done")

test_program(name = "access_log_no_op")

test_program(name = "access_log_missing_config_new")

test_program(name = "access_log_missing_config_destroy")

test_program(name = "access_log_missing_logger_new")

test_program(name = "access_log_missing_logger_log")

test_program(name = "access_log_missing_logger_destroy")

test_program(name = "access_log_config_new_fail")
