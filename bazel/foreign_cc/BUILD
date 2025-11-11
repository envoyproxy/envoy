load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load("@rules_foreign_cc//foreign_cc:make.bzl", "make")
load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_cmake", "envoy_package")

licenses(["notice"])  # Apache 2

exports_files(["icu_data_filter.json"])

envoy_package()

# Global flag to enable parallel builds for all foreign_cc targets
# When enabled, all foreign_cc (or related) builds will use whatever cores available.
# Defaults to false to avoid OOM on developer machines.
bool_flag(
    name = "parallel_builds",
    build_setting_default = False,
)

config_setting(
    name = "parallel_builds_enabled",
    flag_values = {
        ":parallel_builds": "True",
    },
)

configure_make(
    name = "liburing",
    configure_in_place = True,
    lib_source = "@com_github_axboe_liburing//:all",
    tags = [
        "nocompdb",
        "skip_on_windows",
    ],
    targets = [
        "library",
        "install",
    ],
)

envoy_cc_library(
    name = "liburing_linux",
    srcs = [],
    deps = select({
        "//bazel:linux": [":liburing"],
        "//conditions:default": [],
    }),
)

# autotools packages are unusable on Windows as-is
# TODO: Consider our own gperftools.BUILD file as we do with many other packages
configure_make(
    name = "gperftools_build",
    configure_options = [
        "--enable-shared=no",
        "--enable-frame-pointers",
        "--disable-libunwind",
    ] + select({
        "//bazel:apple": ["AR=/usr/bin/ar"],
        "//conditions:default": [],
    }),
    lib_source = "@com_github_gperftools_gperftools//:all",
    linkopts = ["-lpthread"],
    out_static_libs = select({
        "//bazel:debug_tcmalloc": ["libtcmalloc_debug.a"],
        "//conditions:default": ["libtcmalloc_and_profiler.a"],
    }),
    tags = ["skip_on_windows"],
    targets = [
        "install-libLTLIBRARIES install-perftoolsincludeHEADERS",
    ],
    alwayslink = True,
)

# Workaround for https://github.com/bazelbuild/rules_foreign_cc/issues/227
cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    deps = [
        "gperftools_build",
    ],
)

make(
    name = "lz4",
    args = [
        "MOREFLAGS='-fPIC'",
        "BUILD_SHARED=no",
    ],
    lib_source = "@com_github_lz4_lz4//:all",
    out_static_libs = [
        "liblz4.a",
    ],
    tags = ["skip_on_windows"],
    targets = [
        "lib",
        "install",
    ],
    visibility = ["//visibility:public"],
    alwayslink = False,
)

# Kafka client dependency used by Kafka-mesh filter.
# librdkafka build generates extra headers that need to be copied into source to get it to compile.
configure_make(
    name = "librdkafka_build",
    configure_in_place = True,
    configure_options = ["--disable-ssl --disable-gssapi --disable-zstd --disable-curl && cp Makefile.config src/.. && cp config.h src/.."],
    lib_source = "@confluentinc_librdkafka//:all",
    out_static_libs = [
        "librdkafka.a",
        "librdkafka++.a",
    ],
    tags = ["skip_on_windows"],
    targets = [
        "ARFLAGS='' WITH_LDS='n' libs install-subdirs",
    ],
    deps = [":lz4"],
    alwayslink = True,
)

cc_library(
    name = "librdkafka",
    tags = ["skip_on_windows"],
    deps = [
        "librdkafka_build",
    ],
)

configure_make(
    name = "luajit",
    configure_command = "luajit_build.sh",
    env = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//bazel:msan_build": {"ENVOY_CONFIG_MSAN": "1"},
        "//bazel:windows_dbg_build": {"WINDOWS_DBG_BUILD": "debug"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_luajit_luajit//:all",
    out_include_dir = "include/luajit-2.1",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["lua51.lib"],
        "//conditions:default": ["libluajit-5.1.a"],
    }),
    targets = [],
)

configure_make(
    name = "colm",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "AUTOMAKE=automake",
        "ACLOCAL=aclocal",
        "--disable-shared",
        "--enable-static",
    ],
    lib_source = "@net_colm_open_source_colm//:all",
    out_binaries = ["colm"],
    tags = ["skip_on_windows"],
)

configure_make(
    name = "ragel",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "AUTOMAKE=automake",
        "ACLOCAL=aclocal",
        "--disable-manual",
        "--disable-shared",
        "--enable-static",
        "--with-colm=$$EXT_BUILD_DEPS/colm",
    ],
    lib_source = "@net_colm_open_source_ragel//:all",
    out_binaries = ["ragel"],
    tags = ["skip_on_windows"],
    deps = [":colm"],
)

# ICU used by the language detection filter (i18n).
# Generates a minimal configuration and reduces the size of the ICU locale data filter file.
# https://unicode-org.github.io/icu/userguide/icu_data/buildtool.html
configure_make(
    name = "unicode_icu_build",
    build_data = ["//bazel/foreign_cc:icu_data_filter.json"],
    configure_command = "icu4c/source/configure",
    configure_options = [
        "--enable-option-checking",
        "--enable-static",
        "--enable-tools",
        "--disable-draft",
        "--disable-dyload",
        "--disable-extras",
        "--disable-icuio",
        "--disable-plugins",
        "--disable-samples",
        "--disable-shared",
        "--disable-tests",
        "--with-data-packaging=static",
    ],
    data = ["@com_github_unicode_org_icu//:all"],
    env = {
        "CXXFLAGS": "-fPIC -DU_CHARSET_IS_UTF8=1 -DU_USING_ICU_NAMESPACE=0 -DUCONFIG_ONLY_HTML_CONVERSION=1 -DUCONFIG_NO_LEGACY_CONVERSION=1 -DUCONFIG_NO_BREAK_ITERATION=1 -DUCONFIG_NO_COLLATION=1 -DUCONFIG_NO_FORMATTING=1 -DUCONFIG_NO_TRANSLITERATION=1 -DUCONFIG_NO_REGULAR_EXPRESSIONS=1",
        "CFLAGS": "-fPIC",
        "LIBS": "-l:libstdc++.a",
        "ICU_DATA_FILTER_FILE": "$(execpath //bazel/foreign_cc:icu_data_filter.json)",
        "ARFLAGS": "r",
        "PYTHON": "$$EXT_BUILD_ROOT/$(PYTHON3)",
    } | select({
        "//bazel/foreign_cc:parallel_builds_enabled": {
            "MAKEFLAGS": "-j ARFLAGS=r",
        },
        "//bazel:engflow_rbe_x86_64": {
            "MAKEFLAGS": "-j ARFLAGS=r",
        },
        "//bazel:engflow_rbe_aarch64": {
            "MAKEFLAGS": "-j ARFLAGS=r",
        },
        "//conditions:default": {
            "MAKEFLAGS": "-j1 ARFLAGS=r",
        },
    }),
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    lib_source = "@com_github_unicode_org_icu//:all",
    out_static_libs = [
        "libicuuc.a",
        "libicudata.a",
    ],
    tags = ["skip_on_windows"],
    toolchains = ["@rules_python//python:current_py_toolchain"],
    alwayslink = True,
)

cc_library(
    name = "unicode_icu",
    tags = ["skip_on_windows"],
    # Can not be used for the core dataplane due to security concerns
    visibility = ["//contrib/language/filters/http/source:__pkg__"],
    deps = ["unicode_icu_build"],
)

envoy_cmake(
    name = "libsxg",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "SXG_BUILD_EXECUTABLES": "off",
        "SXG_BUILD_SHARED": "off",
        "SXG_BUILD_STATIC": "on",
        "SXG_WITH_CERT_CHAIN": "off",
        "RUN_TEST": "off",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "OPENSSL_ROOT_DIR": "$$EXT_BUILD_DEPS",
        "OPENSSL_INCLUDE_DIR": "$$EXT_BUILD_DEPS/include",
        "OPENSSL_CRYPTO_LIBRARY": "$$EXT_BUILD_DEPS/lib/libcrypto_internal.a",
        "OPENSSL_SSL_LIBRARY": "$$EXT_BUILD_DEPS/lib/libssl_internal.a",
    },
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_google_libsxg//:all",
    out_static_libs = ["libsxg.a"],
    tags = ["skip_on_windows"],
    # Use boringssl alias to select fips vs non-fips version.
    deps = ["//bazel:boringssl"],
)

envoy_cmake(
    name = "ares",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        "CARES_BUILD_TOOLS": "no",
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
    },
    defines = ["CARES_STATICLIB"],
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_c_ares_c_ares//:all",
    linkopts = select({
        "//bazel:apple": ["-lresolv"],
        "//conditions:default": [],
    }),
    out_static_libs = select({
        "//bazel:windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
    postfix_script = select({
        "//bazel:windows_x86_64": "cp -L $$EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/src/lib/ares_nameser.h $$INSTALLDIR/include/ares_nameser.h && cp -L $$EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/include/ares_dns.h $$INSTALLDIR/include/ares_dns.h",
        "//conditions:default": "rm -f $$INSTALLDIR/include/ares_dns.h && cp -L $$EXT_BUILD_ROOT/external/com_github_c_ares_c_ares/include/ares_dns.h $$INSTALLDIR/include/ares_dns.h",
    }),
)

envoy_cmake(
    name = "event",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_MBEDTLS": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "EVENT__DISABLE_TESTS": "on",
        "EVENT__LIBRARY_TYPE": "STATIC",
        # Force _GNU_SOURCE on for Android builds. This would be contained in
        # a 'select' but the downstream macro uses a select on all of these
        # options, and they cannot be nested.
        # If https://github.com/bazelbuild/rules_foreign_cc/issues/289 is fixed
        # this can be removed.
        # More details https://github.com/envoyproxy/envoy-mobile/issues/116
        "_GNU_SOURCE": "on",
    },
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_libevent_libevent//:all",
    out_static_libs = select({
        # macOS organization of libevent is different from Windows/Linux.
        # Including libevent_core is a requirement on those platforms, but
        # results in duplicate symbols when built on macOS.
        # See https://github.com/envoyproxy/envoy-mobile/issues/677 for details.
        "//bazel:apple": [
            "libevent.a",
            "libevent_pthreads.a",
        ],
        "//bazel:windows_x86_64": [
            "event.lib",
            "event_core.lib",
        ],
        "//conditions:default": [
            "libevent.a",
            "libevent_pthreads.a",
            "libevent_core.a",
        ],
    }),
)

envoy_cmake(
    name = "nghttp2",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        "ENABLE_LIB_ONLY": "on",
        "BUILD_SHARED_LIBS": "off",
        "BUILD_STATIC_LIBS": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    cmake_files_dir = "$BUILD_TMPDIR/lib/CMakeFiles",
    debug_cache_entries = {"ENABLE_DEBUG": "on"},
    defines = ["NGHTTP2_STATICLIB"],
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["nghttp2.lib"],
        "//conditions:default": ["libnghttp2.a"],
    }),
)

envoy_cmake(
    name = "wamr",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        # interp by default
        # if want to gain more performance advantage with JIT or AOT
        # please refer to
        # - enable jit and aot. https://github.com/proxy-wasm/proxy-wasm-cpp-host/pull/378
        # - support precompiled wasm. https://github.com/proxy-wasm/proxy-wasm-cpp-host/pull/380
        "WAMR_BUILD_AOT": "0",
        "WAMR_BUILD_FAST_INTERP": "1",
        "WAMR_BUILD_INTERP": "1",
        "WAMR_BUILD_JIT": "0",
        # disable WASI
        "WAMR_BUILD_LIBC_WASI": "0",
        "WAMR_BUILD_LIBC_BUILTIN": "0",
        # MVP
        "WAMR_BUILD_BULK_MEMORY": "1",
        "WAMR_BUILD_REF_TYPES": "1",
        # only for jit and aot
        "WAMR_BUILD_SIMD": "0",
        "WAMR_BUILD_TAIL_CALL": "1",
        # others
        "WAMR_BUILD_WASM_CACHE": "0",
        "WAMR_BUILD_MULTI_MODULE": "0",
        # enable below to enhance development experience
        # name section
        "WAMR_BUILD_CUSTOM_NAME_SECTION": "0",
        "WAMR_BUILD_LOAD_CUSTOM_SECTION": "0",
        # output call stack if meet a trap
        "WAMR_BUILD_DUMP_CALL_STACK": "0",
        # linux perf. only for jit and aot
        "WAMR_BUILD_LINUX_PERF": "0",
    },
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_wamr//:all",
    out_static_libs = ["libvmlib.a"],
    tags = ["skip_on_windows"],
)

envoy_cmake(
    name = "zlib",
    cache_entries = {
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "CMAKE_C_COMPILER_FORCED": "on",
        "SKIP_BUILD_EXAMPLES": "on",
        "BUILD_SHARED_LIBS": "off",

        # The following entries are for zlib-ng. Since zlib and zlib-ng are compatible source
        # codes and CMake ignores unknown cache entries, it is fine to combine it into one
        # dictionary.
        #
        # Reference: https://github.com/zlib-ng/zlib-ng#build-options.
        "ZLIB_COMPAT": "on",
        "ZLIB_ENABLE_TESTS": "off",

        # Warning: Turning WITH_OPTIM to "on" doesn't pass ZlibCompressorImplTest.CallingChecksum.
        "WITH_OPTIM": "on",
        # However turning off SSE4 fixes it.
        "WITH_SSE4": "off",

        # Warning: Turning WITH_NEW_STRATEGIES to "on" doesn't pass gzip compressor fuzz test.
        # Turning this off means falling into NO_QUICK_STRATEGY route.
        "WITH_NEW_STRATEGIES": "off",

        # Only allow aligned address.
        # Reference: https://github.com/zlib-ng/zlib-ng#advanced-build-options.
        "UNALIGNED_OK": "off",
    },
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = select({
        "//bazel:zlib_ng": "@com_github_zlib_ng_zlib_ng//:all",
        "//conditions:default": "@net_zlib//:all",
    }),
    out_static_libs = select({
        "//bazel:windows_x86_64": ["zlib.lib"],
        "//conditions:default": ["libz.a"],
    }),
    postfix_script = select({
        "@platforms//cpu:wasm32": """
            if [[ -f libzlib.a ]]; then
                cp libzlib.a $$INSTALLDIR/lib/libz.a
            fi
        """,
        "//conditions:default": "",
    }),
)

envoy_cmake(
    name = "zstd",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    build_data = ["@com_github_facebook_zstd//:all"],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "ZSTD_BUILD_SHARED": "off",
        "ZSTD_BUILD_STATIC": "on",
    },
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_facebook_zstd//:all",
    out_static_libs = select({
        "//bazel:windows_x86_64": ["zstd_static.lib"],
        "//conditions:default": ["libzstd.a"],
    }),
    working_directory = "build/cmake",
)

envoy_cmake(
    name = "maxmind",
    build_args = select({
        "//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//bazel:engflow_rbe_x86_64": ["-j"],
        "//bazel:engflow_rbe_aarch64": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "BUILD_SHARED_LIBS": "off",
        "BUILD_TESTING": "off",
    },
    defines = ["MAXMIND_STATICLIB"],
    exec_properties = select({
        "//bazel:engflow_rbe_x86_64": {
            "Pool": "linux_x64_large",
        },
        "//bazel:engflow_rbe_aarch64": {
            "Pool": "linux_arm64_small",
        },
        "//conditions:default": {},
    }),
    generate_args = [
        "-G",
        "Ninja",
    ],
    lib_source = "@com_github_maxmind_libmaxminddb//:all",
    out_static_libs = ["libmaxminddb.a"],
    tags = ["skip_on_windows"],
)

envoy_cc_library(
    name = "maxmind_linux_darwin",
    srcs = [],
    deps = select({
        "//bazel:linux": [":maxmind"],
        "//bazel:darwin_any": [":maxmind"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "libcxx_msan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:msan_build": ["@msan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)

cc_library(
    name = "libcxx_tsan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:tsan_build": ["@tsan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)
