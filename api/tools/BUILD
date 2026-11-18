load("@envoy_toolshed//tap:defs.bzl", "tap2pcap")
load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

# Converts a socket tap trace to PCAP:
#   bazel run @envoy_api//tools:tap2pcap <tap .pb/.pb_text> <pcap path>
tap2pcap(
    name = "tap2pcap",
    message = "envoy.data.tap.v3.wrapper_pb2.TraceWrapper",
    proto = "//envoy/data/tap/v3:pkg_py_proto",
    visibility = ["//visibility:public"],
)

py_binary(
    name = "generate_listeners",
    srcs = ["generate_listeners.py"],
    licenses = ["notice"],  # Apache 2
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2:pkg_py_proto",
        "//envoy/config/filter/network/http_connection_manager/v2:pkg_py_proto",
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
