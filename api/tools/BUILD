licenses(["notice"])  # Apache 2

py_binary(
    name = "generate_listeners",
    srcs = ["generate_listeners.py"],
    licenses = ["notice"],  # Apache 2
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:lds_py",
        "//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_py",
    ],
)

py_test(
    name = "generate_listeners_test",
    srcs = ["generate_listeners_test.py"],
    data = [
        "//examples/service_envoy:http_connection_manager.pb",
        "//examples/service_envoy:listeners.pb",
    ],
    visibility = ["//visibility:public"],
    deps = [":generate_listeners"],
)
