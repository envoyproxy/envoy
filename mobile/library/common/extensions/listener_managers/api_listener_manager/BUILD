load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "api_listener_manager_lib",
    srcs = [
        "api_listener_manager.cc",
    ],
    hdrs = [
        "api_listener_manager.h",
    ],
    repository = "@envoy",
    deps = [
        "//library/common/event:provisional_dispatcher_lib",
        "//library/common/http:client_lib",
        "@envoy//envoy/server:api_listener_interface",
        "@envoy//envoy/server:instance_interface",
        "@envoy//envoy/server:listener_manager_interface",
        "@envoy//envoy/thread:thread_interface",
        "@envoy//source/extensions/api_listeners/default_api_listener:api_listener_lib",
        "@envoy//source/server:listener_manager_factory_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)
