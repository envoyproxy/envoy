load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:envoy_internal.bzl", "envoy_select_force_libcpp")
load("//bazel:utils.bzl", "json_data")
load("@bazel_skylib//lib:selects.bzl", "selects")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")
load("@envoy_api//bazel:repository_locations.bzl", API_REPOSITORY_LOCATIONS_SPEC = "REPOSITORY_LOCATIONS_SPEC")
load("@envoy_api//bazel:repository_locations_utils.bzl", "load_repository_locations_spec", "merge_dicts")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "gen_sh_test_runner.sh",
    "sh_test_wrapper.sh",
    "test_for_benchmark_wrapper.sh",
    "repository_locations.bzl",
    "exported_symbols.txt",
])

sh_library(
    name = "volatile_env",
    srcs = ["volatile_env.sh"],
)

genrule(
    name = "gnu_build_id",
    outs = ["gnu_build_id.ldscript"],
    cmd = """
      echo --build-id=0x$$(
          grep BUILD_SCM_REVISION bazel-out/volatile-status.txt \\
        | sed 's/^BUILD_SCM_REVISION //') \\
        > $@
    """,
    # Undocumented attr to depend on workspace status files.
    # https://github.com/bazelbuild/bazel/issues/4942
    stamp = 1,
)

# For macOS, which doesn't have GNU ld's `--build-id` flag.
genrule(
    name = "raw_build_id",
    outs = ["raw_build_id.ldscript"],
    cmd = """
      grep BUILD_SCM_REVISION bazel-out/volatile-status.txt \\
    | sed 's/^BUILD_SCM_REVISION //' \\
    | tr -d '\\n' \\
    > $@
    """,
    # Undocumented attr to depend on workspace status files.
    # https://github.com/bazelbuild/bazel/issues/4942
    stamp = 1,
)

# A target to optionally link C++ standard library dynamically in sanitizer runs.
# TSAN doesn't support libc/libstdc++ static linking per doc:
#   http://releases.llvm.org/8.0.1/tools/clang/docs/ThreadSanitizer.html
cc_library(
    name = "dynamic_stdlib",
    linkopts = envoy_select_force_libcpp(
        ["-lc++"],
        ["-lstdc++"],
    ),
)

cc_library(
    name = "static_stdlib",
    linkopts = select({
        "//bazel:linux": ["-static-libgcc"],
        "//conditions:default": [],
    }),
)

config_setting(
    name = "windows_opt_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "opt",
    },
)

config_setting(
    name = "windows_dbg_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "dbg",
    },
)

config_setting(
    name = "windows_fastbuild_build",
    values = {
        "cpu": "x64_windows",
        "compilation_mode": "fastbuild",
    },
)

config_setting(
    name = "clang_cl_build",
    values = {
        "cpu": "x64_windows",
        "define": "clang_cl=1",
    },
)

config_setting(
    name = "clang_cl_opt_build",
    values = {
        "cpu": "x64_windows",
        "define": "clang_cl=1",
        "compilation_mode": "opt",
    },
)

config_setting(
    name = "clang_cl_dbg_build",
    values = {
        "cpu": "x64_windows",
        "define": "clang_cl=1",
        "compilation_mode": "dbg",
    },
)

config_setting(
    name = "clang_cl_fastbuild_build",
    values = {
        "cpu": "x64_windows",
        "define": "clang_cl=1",
        "compilation_mode": "fastbuild",
    },
)

config_setting(
    name = "opt_build",
    values = {"compilation_mode": "opt"},
)

config_setting(
    name = "fastbuild_build",
    values = {"compilation_mode": "fastbuild"},
)

config_setting(
    name = "dbg_build",
    values = {"compilation_mode": "dbg"},
)

config_setting(
    name = "no_debug_info",
    values = {"define": "no_debug_info=1"},
)

config_setting(
    name = "asan_build",
    values = {"define": "ENVOY_CONFIG_ASAN=1"},
)

config_setting(
    name = "tsan_build",
    values = {"define": "ENVOY_CONFIG_TSAN=1"},
)

config_setting(
    name = "msan_build",
    values = {"define": "ENVOY_CONFIG_MSAN=1"},
)

config_setting(
    name = "coverage_build",
    values = {"define": "ENVOY_CONFIG_COVERAGE=1"},
)

config_setting(
    name = "clang_build",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "clang",
    },
)

config_setting(
    name = "clang_pch_build",
    values = {"define": "ENVOY_CLANG_PCH=1"},
)

config_setting(
    name = "gcc_build_gcc",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "gcc",
    },
)

# This is needed due to a Bazel bug (https://github.com/bazelbuild/bazel/issues/12707)
config_setting(
    name = "gcc_build_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "compiler",
    },
)

selects.config_setting_group(
    name = "gcc_build_compiler_on_linux",
    match_all = [
        ":gcc_build_compiler",
        ":linux",
    ],
)

selects.config_setting_group(
    name = "gcc_build",
    match_any = [
        ":gcc_build_gcc",
        ":gcc_build_compiler_on_linux",
    ],
)

config_setting(
    name = "dynamic_link_tests",
    values = {
        "define": "dynamic_link_tests=true",
    },
)

config_setting(
    name = "disable_tcmalloc",
    values = {"define": "tcmalloc=disabled"},
)

config_setting(
    name = "debug_tcmalloc",
    values = {"define": "tcmalloc=debug"},
)

config_setting(
    name = "gperftools_tcmalloc",
    values = {"define": "tcmalloc=gperftools"},
)

# As select() can't be nested we need these specialized settings to avoid ambiguity when choosing
# tcmalloc's flavor for x86_64 and aarch64 builds.
config_setting(
    name = "disable_tcmalloc_on_linux_x86_64",
    values = {
        "define": "tcmalloc=disabled",
        "cpu": "k8",
    },
)

config_setting(
    name = "gperftools_tcmalloc_on_linux_x86_64",
    values = {
        "define": "tcmalloc=gperftools",
        "cpu": "k8",
    },
)

config_setting(
    name = "debug_tcmalloc_on_linux_x86_64",
    values = {
        "define": "tcmalloc=debug",
        "cpu": "k8",
    },
)

config_setting(
    name = "disable_tcmalloc_on_linux_aarch64",
    values = {
        "define": "tcmalloc=disabled",
        "cpu": "aarch64",
    },
)

config_setting(
    name = "gperftools_tcmalloc_on_linux_aarch64",
    values = {
        "define": "tcmalloc=gperftools",
        "cpu": "aarch64",
    },
)

config_setting(
    name = "debug_tcmalloc_on_linux_aarch64",
    values = {
        "define": "tcmalloc=debug",
        "cpu": "aarch64",
    },
)

config_setting(
    name = "disable_signal_trace",
    values = {"define": "signal_trace=disabled"},
)

config_setting(
    name = "disable_library_autolink",
    values = {"define": "library_autolink=disabled"},
)

config_setting(
    name = "disable_object_dump_on_signal_trace",
    values = {"define": "object_dump_on_signal_trace=disabled"},
)

config_setting(
    name = "disable_deprecated_features",
    values = {"define": "deprecated_features=disabled"},
)

selects.config_setting_group(
    name = "disable_hot_restart_or_admin",
    match_any = [
        "//bazel:disable_hot_restart",
        "//bazel:disable_admin_functionality",
    ],
)

bool_flag(
    name = "http3",
    build_setting_default = True,
    # TODO(keith): make private again https://github.com/bazelbuild/bazel-skylib/issues/404
    # visibility = ["//visibility:private"],
)

config_setting(
    name = "disable_http3_setting",
    flag_values = {
        ":http3": "False",
    },
    # TODO(keith): make private again https://github.com/bazelbuild/bazel-skylib/issues/404
    # visibility = ["//visibility:private"],
)

selects.config_setting_group(
    name = "disable_http3",
    match_any = [
        ":disable_http3_setting",
        ":boringssl_fips",
    ],
)

config_setting(
    name = "disable_admin_html",
    values = {"define": "admin_html=disabled"},
)

config_setting(
    name = "disable_admin_functionality",
    values = {"define": "admin_functionality=disabled"},
)

config_setting(
    name = "disable_hot_restart_setting",
    values = {"define": "hot_restart=disabled"},
    # TODO(keith): make private again https://github.com/bazelbuild/bazel-skylib/issues/404
    # visibility = ["//visibility:private"],
)

selects.config_setting_group(
    name = "disable_hot_restart",
    match_any = [
        ":apple",
        ":disable_hot_restart_setting",
    ],
)

config_setting(
    name = "disable_google_grpc",
    values = {"define": "google_grpc=disabled"},
)

config_setting(
    name = "enable_path_normalization_by_default",
    values = {"define": "path_normalization_by_default=true"},
)

cc_proto_library(
    name = "grpc_health_proto",
    deps = ["@com_github_grpc_grpc//src/proto/grpc/health/v1:_health_proto_only"],
)

config_setting(
    name = "enable_exported_symbols",
    values = {"define": "exported_symbols=enabled"},
)

config_setting(
    name = "enable_log_debug_assert_in_release",
    values = {"define": "log_debug_assert_in_release=enabled"},
)

config_setting(
    name = "enable_log_fast_debug_assert_in_release",
    values = {"define": "log_fast_debug_assert_in_release=enabled"},
)

config_setting(
    name = "disable_known_issue_asserts",
    values = {"define": "disable_known_issue_asserts=true"},
)

config_setting(
    name = "enable_perf_annotation",
    values = {"define": "perf_annotation=enabled"},
)

config_setting(
    name = "enable_perf_tracing",
    values = {"define": "perf_tracing=enabled"},
)

config_setting(
    name = "force_libcpp",
    values = {"define": "force_libcpp=enabled"},
)

config_setting(
    name = "boringssl_fips",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    values = {"define": "boringssl=fips"},
)

config_setting(
    name = "boringssl_disabled",
    values = {"define": "boringssl=disabled"},
)

config_setting(
    name = "zlib_ng",
    constraint_values = [
        "@platforms//os:linux",
    ],
    values = {"define": "zlib=ng"},
)

# TODO: consider converting WAVM VM support to an extension (https://github.com/envoyproxy/envoy/issues/12574)
config_setting(
    name = "wasm_wavm",
    values = {"define": "wasm=wavm"},
)

config_setting(
    name = "wasm_v8",
    values = {"define": "wasm=v8"},
)

config_setting(
    name = "wasm_wamr",
    values = {"define": "wasm=wamr"},
)

config_setting(
    name = "wasm_wasmtime",
    values = {"define": "wasm=wasmtime"},
)

config_setting(
    name = "wasm_disabled",
    values = {"define": "wasm=disabled"},
)

# This config setting enables Universal Header Validator and disables
# HTTP header compliance checks in codecs.
# This setting is temporary to transition header validation into UHV without
# impacting production builds of Envoy.
# This setting is enabled for the bazel.compile_time_options CI target.
config_setting(
    name = "uhv_enabled",
    values = {"define": "uhv=enabled"},
)

# Alias pointing to the selected version of BoringSSL:
# - BoringSSL FIPS from @boringssl_fips//:ssl,
# - non-FIPS BoringSSL from @boringssl//:ssl.
alias(
    name = "boringssl",
    actual = select({
        "//bazel:boringssl_fips": "@boringssl_fips//:ssl",
        "//conditions:default": "@boringssl//:ssl",
    }),
)

alias(
    name = "boringcrypto",
    actual = select({
        "//bazel:boringssl_fips": "@boringssl_fips//:crypto",
        "//conditions:default": "@boringssl//:crypto",
    }),
)

config_setting(
    name = "linux_x86_64",
    values = {"cpu": "k8"},
)

config_setting(
    name = "linux_aarch64",
    values = {"cpu": "aarch64"},
)

config_setting(
    name = "linux_ppc",
    values = {"cpu": "ppc"},
)

config_setting(
    name = "linux_s390x",
    values = {"cpu": "s390x"},
)

config_setting(
    name = "linux_mips64",
    values = {"cpu": "mips64"},
)

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
)

# Configuration settings to make doing selects for Apple vs non-Apple platforms
# easier. More details: https://docs.bazel.build/versions/master/configurable-attributes.html#config_settingaliasing
config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
)

config_setting(
    name = "darwin_x86_64",
    values = {"cpu": "darwin_x86_64"},
)

config_setting(
    name = "darwin_arm64",
    values = {"cpu": "darwin_arm64"},
)

config_setting(
    name = "ios_i386",
    values = {"cpu": "ios_i386"},
)

config_setting(
    name = "ios_x86_64",
    values = {"cpu": "ios_x86_64"},
)

config_setting(
    name = "ios_armv7",
    values = {"cpu": "ios_armv7"},
)

config_setting(
    name = "ios_armv7s",
    values = {"cpu": "ios_armv7s"},
)

config_setting(
    name = "ios_arm64",
    values = {"cpu": "ios_arm64"},
)

config_setting(
    name = "ios_sim_arm64",
    values = {"cpu": "ios_sim_arm64"},
)

config_setting(
    name = "ios_arm64e",
    values = {"cpu": "ios_arm64e"},
)

config_setting(
    name = "manual_stamp",
    values = {"define": "manual_stamp=manual_stamp"},
)

config_setting(
    name = "android",
    constraint_values = [
        "@platforms//os:android",
    ],
)

config_setting(
    name = "android_logger",
    values = {"define": "logger=android"},
)

config_setting(
    name = "libfuzzer_coverage",
    define_values = {
        "FUZZING_ENGINE": "libfuzzer",
        "ENVOY_CONFIG_COVERAGE": "1",
    },
)

config_setting(
    name = "libfuzzer",
    define_values = {"FUZZING_ENGINE": "libfuzzer"},
)

config_setting(
    name = "oss_fuzz",
    define_values = {"FUZZING_ENGINE": "oss-fuzz"},
)

alias(
    name = "fuzzing_engine",
    actual = select({
        ":libfuzzer": "@rules_fuzzing//fuzzing/engines:libfuzzer",
        ":oss_fuzz": "@rules_fuzzing_oss_fuzz//:oss_fuzz_engine",
        "//conditions:default": "//test/fuzz:fuzz_runner_engine",
    }),
)

selects.config_setting_group(
    name = "apple",
    match_any = [
        ":darwin",
        ":darwin_arm64",
        ":darwin_x86_64",
        ":ios_arm64",
        ":ios_arm64e",
        ":ios_armv7",
        ":ios_armv7s",
        ":ios_i386",
        ":ios_sim_arm64",
        ":ios_x86_64",
    ],
)

selects.config_setting_group(
    name = "apple_dbg",
    match_all = [
        ":apple",
        ":dbg_build",
    ],
)

selects.config_setting_group(
    name = "apple_fastbuild",
    match_all = [
        ":apple",
        ":fastbuild_build",
    ],
)

selects.config_setting_group(
    name = "apple_non_opt",
    match_any = [
        ":apple_dbg",
        ":apple_fastbuild",
    ],
)

selects.config_setting_group(
    name = "linux",
    match_any = [
        ":linux_aarch64",
        ":linux_mips64",
        ":linux_ppc",
        ":linux_s390x",
        ":linux_x86_64",
    ],
)

selects.config_setting_group(
    name = "x86",
    match_any = [
        ":darwin_x86_64",
        ":ios_x86_64",
        ":linux_x86_64",
        ":windows_x86_64",
    ],
)

selects.config_setting_group(
    name = "not_x86",
    match_any = [
        ":darwin_arm64",
        ":ios_arm64",
        ":ios_arm64e",
        ":ios_armv7",
        ":ios_armv7s",
        ":ios_i386",
        ":ios_sim_arm64",
        ":linux_aarch64",
        ":linux_mips64",
        ":linux_ppc",
        ":linux_s390x",
    ],
)

selects.config_setting_group(
    name = "not_x86_or_wasm_disabled",
    match_any = [
        ":not_x86",
        ":wasm_disabled",
    ],
)

alias(
    name = "remote_jdk11",
    actual = "@bazel_tools//tools/jdk:remote_jdk11",
)

alias(
    name = "windows",
    actual = "@bazel_tools//src/conditions:windows",
)

json_data(
    name = "repository_locations",
    data = load_repository_locations_spec(REPOSITORY_LOCATIONS_SPEC),
)

json_data(
    name = "all_repository_locations",
    data = merge_dicts(
        load_repository_locations_spec(REPOSITORY_LOCATIONS_SPEC),
        load_repository_locations_spec(API_REPOSITORY_LOCATIONS_SPEC),
    ),
)

platform(
    name = "android_aarch64",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:android",
    ],
)

platform(
    name = "android_armeabi",
    constraint_values = [
        "@platforms//cpu:armv7",
        "@platforms//os:android",
    ],
)

platform(
    name = "android_x86",
    constraint_values = [
        "@platforms//cpu:x86_32",
        "@platforms//os:android",
    ],
)

platform(
    name = "android_x86_64",
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:android",
    ],
)

platform(
    name = "macos_x86_64",
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:macos",
    ],
)

platform(
    name = "macos_arm64",
    constraint_values = [
        "@platforms//cpu:arm64",
        "@platforms//os:macos",
    ],
)

platform(
    name = "ios_x86_64_platform",  # TODO(keith): Resolve duplicate name issue
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:ios",
        "@build_bazel_apple_support//constraints:simulator",
    ],
)

platform(
    name = "ios_sim_arm64_platform",  # TODO(keith): Resolve duplicate name issue
    constraint_values = [
        "@platforms//cpu:arm64",
        "@platforms//os:ios",
        "@build_bazel_apple_support//constraints:simulator",
    ],
)

platform(
    name = "ios_arm64_platform",  # TODO(keith): Resolve duplicate name issue
    constraint_values = [
        "@platforms//cpu:arm64",
        "@platforms//os:ios",
        "@build_bazel_apple_support//constraints:device",
    ],
)
