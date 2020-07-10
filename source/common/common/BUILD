load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_include_prefix",
    "envoy_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "assert_lib",
    srcs = ["assert.cc"],
    hdrs = ["assert.h"],
    external_deps = [
        "abseil_base",
        "abseil_synchronization",
    ],
    deps = [":minimal_logger_lib"],
)

envoy_cc_library(
    name = "debug_recursion_checker_lib",
    hdrs = ["debug_recursion_checker.h"],
    deps = [":assert_lib"],
)

envoy_cc_library(
    name = "backoff_lib",
    srcs = ["backoff_strategy.cc"],
    hdrs = ["backoff_strategy.h"],
    deps = [
        ":assert_lib",
        "//include/envoy/common:backoff_strategy_interface",
        "//include/envoy/common:random_generator_interface",
    ],
)

envoy_cc_library(
    name = "base64_lib",
    srcs = ["base64.cc"],
    hdrs = ["base64.h"],
    deps = [
        ":assert_lib",
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
    deps = [
        ":assert_lib",
    ],
)

envoy_cc_library(
    name = "compiler_requirements_lib",
    hdrs = ["compiler_requirements.h"],
)

envoy_cc_library(
    name = "documentation_url_lib",
    hdrs = ["documentation_url.h"],
)

envoy_cc_library(
    name = "empty_string",
    hdrs = ["empty_string.h"],
)

envoy_cc_library(
    name = "enum_to_int",
    hdrs = ["enum_to_int.h"],
)

# fmt_lib is automatically a dependency of all envoy_cc_library definitions.
envoy_basic_cc_library(
    name = "fmt_lib",
    hdrs = ["fmt.h"],
    external_deps = [
        "abseil_strings",
        "fmtlib",
    ],
    include_prefix = envoy_include_prefix(package_name()),
    deps = ["//include/envoy/common:base_includes"],
)

envoy_cc_library(
    name = "hash_lib",
    srcs = ["hash.cc"],
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
    name = "mem_block_builder_lib",
    hdrs = ["mem_block_builder.h"],
    deps = [":assert_lib"],
)

# Contains macros and helpers for dumpState utilities
envoy_cc_library(
    name = "dump_state_utils",
    hdrs = ["dump_state_utils.h"],
)

# Contains minimal code for logging to stderr.
envoy_cc_library(
    name = "minimal_logger_lib",
    srcs = ["logger.cc"],
    hdrs = ["logger.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":base_logger_lib",
        ":lock_guard_lib",
        ":macros",
        ":non_copyable",
    ] + select({
        "//bazel:android_logger": ["logger_impl_lib_android"],
        "//conditions:default": ["logger_impl_lib_standard"],
    }),
)

envoy_cc_library(
    name = "base_logger_lib",
    srcs = ["base_logger.cc"],
    hdrs = ["base_logger.h"],
)

envoy_cc_library(
    name = "logger_impl_lib_standard",
    hdrs = ["standard/logger_impl.h"],
    strip_include_prefix = "standard",
    deps = [":base_logger_lib"],
)

envoy_cc_library(
    name = "logger_impl_lib_android",
    srcs = select({
        "//bazel:android_logger": ["android/logger_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:android_logger": ["android/logger_impl.h"],
        "//conditions:default": [],
    }),
    strip_include_prefix = "android",
    deps = [":base_logger_lib"],
)

envoy_cc_library(
    name = "mutex_tracer_lib",
    srcs = ["mutex_tracer_impl.cc"],
    hdrs = ["mutex_tracer_impl.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":assert_lib",
        "//include/envoy/common:mutex_tracer",
    ],
)

# All non-essential logger delegates should go here to reduce dependencies that
# minimal_logger_lib maintains.
envoy_cc_library(
    name = "logger_lib",
    srcs = ["logger_delegates.cc"],
    hdrs = ["logger_delegates.h"],
    deps = [
        ":dump_state_utils",
        ":macros",
        ":minimal_logger_lib",
        "//include/envoy/access_log:access_log_interface",
    ],
)

envoy_cc_library(
    name = "basic_resource_lib",
    hdrs = ["basic_resource_impl.h"],
    deps = [
        "//include/envoy/common:resource_interface",
        "//include/envoy/runtime:runtime_interface",
    ],
)

envoy_cc_library(
    name = "macros",
    hdrs = ["macros.h"],
)

envoy_cc_library(
    name = "matchers_lib",
    srcs = ["matchers.cc"],
    hdrs = ["matchers.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":utility_lib",
        "//include/envoy/common:matchers_interface",
        "//source/common/common:regex_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:path_utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "random_generator_lib",
    srcs = [
        "random_generator.cc",
    ],
    hdrs = [
        "random_generator.h",
    ],
    external_deps = ["ssl"],
    deps = [
        ":assert_lib",
        "//include/envoy/common:random_generator_interface",
    ],
)

envoy_cc_library(
    name = "regex_lib",
    srcs = ["regex.cc"],
    hdrs = ["regex.h"],
    deps = [
        ":assert_lib",
        "//include/envoy/common:regex_interface",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "@com_googlesource_code_re2//:re2",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "non_copyable",
    hdrs = ["non_copyable.h"],
)

envoy_cc_library(
    name = "phantom",
    hdrs = ["phantom.h"],
)

envoy_cc_library(
    name = "scope_tracker",
    hdrs = ["scope_tracker.h"],
    deps = [
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/event:dispatcher_interface",
    ],
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
    name = "thread_synchronizer_lib",
    srcs = ["thread_synchronizer.cc"],
    hdrs = ["thread_synchronizer.h"],
    external_deps = ["abseil_synchronization"],
    deps = [
        ":assert_lib",
    ],
)

envoy_cc_library(
    name = "thread_lib",
    hdrs = ["thread.h"],
    external_deps = ["abseil_synchronization"],
    deps = envoy_cc_platform_dep("thread_impl_lib") + [
        ":non_copyable",
    ],
)

envoy_cc_posix_library(
    name = "thread_impl_lib",
    srcs = ["posix/thread_impl.cc"],
    hdrs = ["posix/thread_impl.h"],
    strip_include_prefix = "posix",
    deps = [
        ":assert_lib",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_win32_library(
    name = "thread_impl_lib",
    srcs = ["win32/thread_impl.cc"],
    hdrs = ["win32/thread_impl.h"],
    strip_include_prefix = "win32",
    deps = [
        ":assert_lib",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "lock_guard_lib",
    hdrs = ["lock_guard.h"],
    deps = [
        ":thread_annotations",
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
        ":non_copyable",
        "//include/envoy/common:interval_set_interface",
        "//include/envoy/common:time_interface",
        "//source/common/singleton:const_singleton",
    ],
)

genrule(
    name = "generate_version_number",
    srcs = ["//:VERSION"],
    outs = ["version_number.h"],
    cmd = """echo "#define BUILD_VERSION_NUMBER \\"$$(cat $<)\\"" >$@""",
)

genrule(
    name = "generate_version_linkstamp",
    outs = ["lib/version_linkstamp.h"],
    cmd = select({
        # Only iOS builds typically follow this logic, OS/X is built as a normal binary
        "//bazel:apple": "$(location :generate_version_linkstamp.sh) Library >> $@",
        "//conditions:default": "$(location :generate_version_linkstamp.sh) >> $@",
    }),
    # Undocumented attr to depend on workspace status files.
    # https://github.com/bazelbuild/bazel/issues/4942
    # Used here because generate_version_linkstamp.sh depends on the workspace status files.
    stamp = 1,
    tools = [":generate_version_linkstamp.sh"],
)

genrule(
    name = "generate_version_linkstamp_empty",
    outs = ["empty/version_linkstamp.h"],
    cmd = """>$@""",
)

envoy_cc_library(
    name = "version_includes",
    hdrs = [
        "version.h",
        ":generate_version_number",
    ],
    deps = [
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "version_lib",
    srcs = ["version.cc"],
    hdrs = select({
        "//bazel:manual_stamp": [":generate_version_linkstamp"],
        # By default the header file is empty.
        # This is done so that the definitions linked via the linkstamp rule don't cause collisions.
        "//conditions:default": [":generate_version_linkstamp_empty"],
    }),
    copts = envoy_select_boringssl(
        ["-DENVOY_SSL_VERSION=\\\"BoringSSL-FIPS\\\""],
        ["-DENVOY_SSL_VERSION=\\\"BoringSSL\\\""],
    ),
    linkstamp = "version_linkstamp.cc",
    strip_include_prefix = select({
        "//bazel:manual_stamp": "lib",
        "//conditions:default": "empty",
    }),
    deps = [
        ":version_includes",
        "//source/common/common:macros",
        "//source/common/protobuf:utility_lib",
    ],
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
    name = "perf_annotation_lib",
    srcs = ["perf_annotation.cc"],
    hdrs = ["perf_annotation.h"],
    deps = [
        ":assert_lib",
        ":thread_annotations",
        ":thread_lib",
        ":utility_lib",
    ],
)

envoy_cc_library(
    name = "scalar_to_byte_vector_lib",
    hdrs = ["scalar_to_byte_vector.h"],
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

envoy_cc_library(
    name = "statusor_lib",
    hdrs = ["statusor.h"],
    deps = [
        "//third_party/statusor:statusor_lib",
    ],
)
