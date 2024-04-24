load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "consecutive_errors_monitor_lib",
    srcs = ["consecutive_errors_monitor.cc"],
    deps = [
        #"//envoy/access_log:access_log_interface",
        #"//envoy/event:dispatcher_interface",
        #"//envoy/event:timer_interface",
        #"//envoy/runtime:runtime_interface",
        #"//envoy/upstream:outlier_detection_interface",
        #"//envoy/upstream:upstream_interface",
        #"//source/common/common:assert_lib",
        #"//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        #"//source/common/http:codes_lib",
        "//source/extensions/outlier_detection_monitors/common:outlier_detection_monitors_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        #"@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        #"@envoy_api//envoy/data/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/outlier_detection_monitors/consecutive_errors/v3:pkg_cc_proto",
    ],
)
