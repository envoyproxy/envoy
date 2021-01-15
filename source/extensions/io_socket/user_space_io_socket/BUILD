load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "peer_buffer_lib",
    hdrs = ["peer_buffer.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_extension(
    name = "users_space_file_event_lib",
    srcs = [
        "user_space_file_event_impl.cc",
    ],
    hdrs = [
        "user_space_file_event_impl.h",
    ],
    security_posture = "unknown",
    status = "alpha",
    undocumented = True,
    deps = [
        ":peer_buffer_lib",
        "//source/common/event:dispatcher_includes",
    ],
)
