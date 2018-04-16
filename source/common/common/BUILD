licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_include_prefix",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "assert_lib",
    hdrs = ["assert.h"],
    deps = [":logger_lib"],
)

envoy_cc_library(
    name = "base64_lib",
    srcs = ["base64.cc"],
    hdrs = ["base64.h"],
    deps = [
        ":empty_string",
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "byte_order_lib",
    hdrs = ["byte_order.h"],
)

envoy_cc_library(
    name = "c_smart_ptr_lib",
    hdrs = ["c_smart_ptr.h"],
)

envoy_cc_library(
    name = "cleanup_lib",
    hdrs = ["cleanup.h"],
)

envoy_cc_library(
    name = "compiler_requirements_lib",
    hdrs = ["compiler_requirements.h"],
)

envoy_cc_library(
    name = "empty_string",
    hdrs = ["empty_string.h"],
)

envoy_cc_library(
    name = "enum_to_int",
    hdrs = ["enum_to_int.h"],
)

# fmt_lib is automatically a dependency of all enovy_cc_library definitions.
envoy_basic_cc_library(
    name = "fmt_lib",
    hdrs = ["fmt.h"],
    include_prefix = envoy_include_prefix(PACKAGE_NAME),
)

envoy_cc_library(
    name = "hash_lib",
    hdrs = ["hash.h"],
    external_deps = ["xxhash"],
)

envoy_cc_library(
    name = "hex_lib",
    srcs = ["hex.cc"],
    hdrs = ["hex.h"],
    deps = [":utility_lib"],
)

envoy_cc_library(
    name = "linked_object",
    hdrs = ["linked_object.h"],
    deps = [":assert_lib"],
)

envoy_cc_library(
    name = "logger_lib",
    srcs = ["logger.cc"],
    hdrs = ["logger.h"],
    deps = [
        ":macros",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "macros",
    hdrs = ["macros.h"],
)

envoy_cc_library(
    name = "non_copyable",
    hdrs = ["non_copyable.h"],
)

envoy_cc_library(
    name = "stl_helpers",
    hdrs = ["stl_helpers.h"],
)

envoy_cc_library(
    name = "thread_annotations",
    hdrs = ["thread_annotations.h"],
    external_deps = ["abseil_base"],
)

envoy_cc_library(
    name = "thread_lib",
    srcs = ["thread.cc"],
    hdrs = ["thread.h"],
    deps = [
        ":assert_lib",
        ":macros",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":assert_lib",
        ":hash_lib",
        "//include/envoy/common:interval_set_interface",
        "//include/envoy/common:time_interface",
    ],
)

genrule(
    name = "generate_version_number",
    srcs = ["@envoy_api//:VERSION"],
    outs = ["version_number.h"],
    cmd = """echo "#define BUILD_VERSION_NUMBER \\"$$(cat $<)\\"" >$@""",
)

envoy_cc_library(
    name = "version_includes",
    hdrs = [
        "version.h",
        ":generate_version_number",
    ],
)

envoy_cc_library(
    name = "version_lib",
    srcs = ["version.cc"],
    linkstamp = "version_linkstamp.cc",
    deps = [
        ":version_includes",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "to_lower_table_lib",
    srcs = ["to_lower_table.cc"],
    hdrs = ["to_lower_table.h"],
)

envoy_cc_library(
    name = "callback_impl_lib",
    hdrs = ["callback_impl.h"],
    deps = [
        ":assert_lib",
        "//include/envoy/common:callback",
    ],
)

envoy_cc_library(
    name = "shared_memory_hash_set_lib",
    hdrs = ["shared_memory_hash_set.h"],
    deps = [
        ":assert_lib",
        ":logger_lib",
    ],
)

envoy_cc_library(
    name = "perf_annotation_lib",
    srcs = ["perf_annotation.cc"],
    hdrs = ["perf_annotation.h"],
    deps = [
        ":assert_lib",
        ":utility_lib",
    ],
)

envoy_cc_library(
    name = "token_bucket_impl_lib",
    srcs = ["token_bucket_impl.cc"],
    hdrs = ["token_bucket_impl.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/common:token_bucket_interface",
        "//source/common/common:utility_lib",
    ],
)
