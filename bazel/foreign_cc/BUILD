licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cmake_external", "envoy_package")
load("@rules_foreign_cc//tools/build_defs:configure.bzl", "configure_make")

envoy_package()

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
    make_commands = ["make install-libLTLIBRARIES install-perftoolsincludeHEADERS"],
    static_libraries = select({
        "//bazel:debug_tcmalloc": ["libtcmalloc_debug.a"],
        "//conditions:default": ["libtcmalloc_and_profiler.a"],
    }),
    tags = ["skip_on_windows"],
)

# Workaround for https://github.com/bazelbuild/rules_foreign_cc/issues/227
cc_library(
    name = "gperftools",
    tags = ["skip_on_windows"],
    deps = [
        "gperftools_build",
    ],
)

# autotools packages are unusable on Windows as-is
# TODO: Consider our own luajit.BUILD file as we do with many other packages
configure_make(
    name = "luajit",
    configure_command = "build.py",
    configure_env_vars = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_luajit_luajit//:all",
    make_commands = [],
    out_include_dir = "include/luajit-2.1",
    static_libraries = [
        "libluajit-5.1.a",
    ],
    tags = ["skip_on_windows"],
)

configure_make(
    name = "moonjit",
    configure_command = "build.py",
    configure_env_vars = select({
        # This shouldn't be needed! See
        # https://github.com/envoyproxy/envoy/issues/6084
        # TODO(htuch): Remove when #6084 is fixed
        "//bazel:asan_build": {"ENVOY_CONFIG_ASAN": "1"},
        "//conditions:default": {},
    }),
    lib_source = "@com_github_moonjit_moonjit//:all",
    make_commands = [],
    out_include_dir = "include/moonjit-2.2",
    static_libraries = [
        "libluajit-5.1.a",
    ],
    tags = ["skip_on_windows"],
)

envoy_cmake_external(
    name = "ares",
    cache_entries = {
        "CARES_SHARED": "no",
        "CARES_STATIC": "on",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    defines = ["CARES_STATICLIB"],
    lib_source = "@com_github_c_ares_c_ares//:all",
    static_libraries = select({
        "//bazel:windows_x86_64": ["cares.lib"],
        "//conditions:default": ["libcares.a"],
    }),
)

envoy_cmake_external(
    name = "curl",
    cache_entries = {
        "BUILD_CURL_EXE": "off",
        "BUILD_TESTING": "off",
        "BUILD_SHARED_LIBS": "off",
        "CURL_HIDDEN_SYMBOLS": "off",
        "CMAKE_USE_LIBSSH2": "off",
        "CURL_BROTLI": "off",
        "CMAKE_USE_GSSAPI": "off",
        "HTTP_ONLY": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
        # C-Ares.
        "ENABLE_ARES": "on",
        "CARES_LIBRARY": "$EXT_BUILD_DEPS/ares",
        "CARES_INCLUDE_DIR": "$EXT_BUILD_DEPS/ares/include",
        # SSL (via Envoy's SSL dependency) is disabled, curl's CMake uses
        # FindOpenSSL.cmake which fails at what looks like version parsing
        # (the libraries are found ok).
        "CURL_CA_PATH": "none",
        "CMAKE_USE_OPENSSL": "off",
        "OPENSSL_ROOT_DIR": "$EXT_BUILD_DEPS",
        # NGHTTP2.
        "USE_NGHTTP2": "on",
        "NGHTTP2_LIBRARY": "$EXT_BUILD_DEPS/nghttp2",
        "NGHTTP2_INCLUDE_DIR": "$EXT_BUILD_DEPS/nghttp2/include",
        # ZLIB.
        "CURL_ZLIB": "on",
        "ZLIB_LIBRARY": "$EXT_BUILD_DEPS/zlib",
        "ZLIB_INCLUDE_DIR": "$EXT_BUILD_DEPS/zlib/include",
        "CMAKE_CXX_COMPILER_FORCED": "on",
        "CMAKE_C_FLAGS_BAZEL": "-fPIC",
    },
    defines = ["CURL_STATICLIB"],
    generate_crosstool_file = True,
    lib_source = "@com_github_curl//:all",
    static_libraries = select({
        "//bazel:windows_x86_64": ["libcurl.lib"],
        "//conditions:default": ["libcurl.a"],
    }),
    deps = [
        ":ares",
        ":nghttp2",
        ":zlib",
        "//external:ssl",
    ],
)

envoy_cmake_external(
    name = "event",
    cache_entries = {
        "EVENT__DISABLE_OPENSSL": "on",
        "EVENT__DISABLE_REGRESS": "on",
        "EVENT__DISABLE_TESTS": "on",
        "EVENT__LIBRARY_TYPE": "STATIC",
        # Force _GNU_SOURCE on for Android builds. This would be contained in
        # a 'select' but the downstream macro uses a select on all of these
        # options, and they cannot be nested.
        # If https://github.com/bazelbuild/rules_foreign_cc/issues/289 is fixed
        # this can be removed.
        # More details https://github.com/lyft/envoy-mobile/issues/116
        "_GNU_SOURCE": "on",
    },
    lib_source = "@com_github_libevent_libevent//:all",
    static_libraries = select({
        # macOS organization of libevent is different from Windows/Linux.
        # Including libevent_core is a requirement on those platforms, but
        # results in duplicate symbols when built on macOS.
        # See https://github.com/lyft/envoy-mobile/issues/677 for details.
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

envoy_cmake_external(
    name = "nghttp2",
    cache_entries = {
        "ENABLE_LIB_ONLY": "on",
        "ENABLE_SHARED_LIB": "off",
        "ENABLE_STATIC_LIB": "on",
        "CMAKE_INSTALL_LIBDIR": "lib",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    cmake_files_dir = "$BUILD_TMPDIR/lib/CMakeFiles",
    debug_cache_entries = {"ENABLE_DEBUG": "on"},
    defines = ["NGHTTP2_STATICLIB"],
    lib_source = "@com_github_nghttp2_nghttp2//:all",
    static_libraries = select({
        "//bazel:windows_x86_64": ["nghttp2_static.lib"],
        "//conditions:default": ["libnghttp2_static.a"],
    }),
)

envoy_cmake_external(
    name = "yaml",
    cache_entries = {
        "YAML_CPP_BUILD_TESTS": "off",
        "YAML_CPP_BUILD_TOOLS": "off",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    lib_source = "@com_github_jbeder_yaml_cpp//:all",
    static_libraries = select({
        "//bazel:windows_x86_64": ["libyaml-cpp.lib"],
        "//conditions:default": ["libyaml-cpp.a"],
    }),
)

envoy_cmake_external(
    name = "zlib",
    cache_entries = {
        "BUILD_SHARED_LIBS": "off",
        "CMAKE_CXX_COMPILER_FORCED": "on",
    },
    lib_source = "@net_zlib//:all",
    static_libraries = select({
        "//bazel:windows_x86_64": ["zlibstatic.lib"],
        "//conditions:default": ["libz.a"],
    }),
)
