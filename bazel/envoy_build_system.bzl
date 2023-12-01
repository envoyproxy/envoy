# The main Envoy bazel file. Load this file for all Envoy-specific build macros
# and rules that you'd like to use in your BUILD files.
load("@rules_foreign_cc//foreign_cc:cmake.bzl", "cmake")
load(":envoy_binary.bzl", _envoy_cc_binary = "envoy_cc_binary")
load(":envoy_internal.bzl", "envoy_external_dep_path")
load(
    ":envoy_library.bzl",
    _envoy_basic_cc_library = "envoy_basic_cc_library",
    _envoy_cc_contrib_extension = "envoy_cc_contrib_extension",
    _envoy_cc_extension = "envoy_cc_extension",
    _envoy_cc_library = "envoy_cc_library",
    _envoy_cc_linux_library = "envoy_cc_linux_library",
    _envoy_cc_posix_library = "envoy_cc_posix_library",
    _envoy_cc_posix_without_linux_library = "envoy_cc_posix_without_linux_library",
    _envoy_cc_win32_library = "envoy_cc_win32_library",
    _envoy_proto_library = "envoy_proto_library",
)
load(":envoy_pch.bzl", _envoy_pch_library = "envoy_pch_library")
load(
    ":envoy_select.bzl",
    _envoy_select_admin_functionality = "envoy_select_admin_functionality",
    _envoy_select_admin_html = "envoy_select_admin_html",
    _envoy_select_admin_no_html = "envoy_select_admin_no_html",
    _envoy_select_boringssl = "envoy_select_boringssl",
    _envoy_select_disable_exceptions = "envoy_select_disable_exceptions",
    _envoy_select_disable_logging = "envoy_select_disable_logging",
    _envoy_select_enable_http3 = "envoy_select_enable_http3",
    _envoy_select_enable_http_datagrams = "envoy_select_enable_http_datagrams",
    _envoy_select_enable_yaml = "envoy_select_enable_yaml",
    _envoy_select_envoy_mobile_listener = "envoy_select_envoy_mobile_listener",
    _envoy_select_envoy_mobile_xds = "envoy_select_envoy_mobile_xds",
    _envoy_select_google_grpc = "envoy_select_google_grpc",
    _envoy_select_hot_restart = "envoy_select_hot_restart",
    _envoy_select_signal_trace = "envoy_select_signal_trace",
    _envoy_select_static_extension_registration = "envoy_select_static_extension_registration",
    _envoy_select_wasm_cpp_tests = "envoy_select_wasm_cpp_tests",
    _envoy_select_wasm_rust_tests = "envoy_select_wasm_rust_tests",
    _envoy_select_wasm_v8 = "envoy_select_wasm_v8",
    _envoy_select_wasm_wamr = "envoy_select_wasm_wamr",
    _envoy_select_wasm_wasmtime = "envoy_select_wasm_wasmtime",
    _envoy_select_wasm_wavm = "envoy_select_wasm_wavm",
)
load(
    ":envoy_test.bzl",
    _envoy_benchmark_test = "envoy_benchmark_test",
    _envoy_cc_benchmark_binary = "envoy_cc_benchmark_binary",
    _envoy_cc_fuzz_test = "envoy_cc_fuzz_test",
    _envoy_cc_mock = "envoy_cc_mock",
    _envoy_cc_test = "envoy_cc_test",
    _envoy_cc_test_binary = "envoy_cc_test_binary",
    _envoy_cc_test_library = "envoy_cc_test_library",
    _envoy_py_test = "envoy_py_test",
    _envoy_py_test_binary = "envoy_py_test_binary",
    _envoy_sh_test = "envoy_sh_test",
)
load(
    ":envoy_mobile_defines.bzl",
    _envoy_mobile_defines = "envoy_mobile_defines",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "CONTRIB_EXTENSION_PACKAGE_VISIBILITY",
    "EXTENSION_PACKAGE_VISIBILITY",
)
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def envoy_package(default_visibility = ["//visibility:public"]):
    native.package(default_visibility = default_visibility)

def envoy_extension_package(enabled_default = True, default_visibility = EXTENSION_PACKAGE_VISIBILITY):
    native.package(default_visibility = default_visibility)

    bool_flag(
        name = "enabled",
        build_setting_default = enabled_default,
    )

    native.config_setting(
        name = "is_enabled",
        flag_values = {":enabled": "True"},
    )

def envoy_mobile_package(default_visibility = ["//visibility:public"]):
    envoy_extension_package(default_visibility = default_visibility)

def envoy_contrib_package():
    envoy_extension_package(default_visibility = CONTRIB_EXTENSION_PACKAGE_VISIBILITY)

# A genrule variant that can output a directory. This is useful when doing things like
# generating a fuzz corpus mechanically.
def _envoy_directory_genrule_impl(ctx):
    tree = ctx.actions.declare_directory(ctx.attr.name + ".outputs")
    ctx.actions.run_shell(
        inputs = ctx.files.srcs,
        tools = ctx.files.tools,
        outputs = [tree],
        command = "mkdir -p " + tree.path + " && " + ctx.expand_location(ctx.attr.cmd),
        env = {"GENRULE_OUTPUT_DIR": tree.path},
        toolchain = None,
    )
    return [DefaultInfo(files = depset([tree]))]

envoy_directory_genrule = rule(
    implementation = _envoy_directory_genrule_impl,
    attrs = {
        "srcs": attr.label_list(),
        "cmd": attr.string(),
        "tools": attr.label_list(),
    },
)

# External CMake C++ library targets should be specified with this function. This defaults
# to building the dependencies with ninja
def envoy_cmake(
        name,
        cache_entries = {},
        debug_cache_entries = {},
        default_cache_entries = {"CMAKE_BUILD_TYPE": "Bazel"},
        lib_source = "",
        postfix_script = "",
        copy_pdb = False,
        pdb_name = "",
        cmake_files_dir = "$BUILD_TMPDIR/CMakeFiles",
        generate_crosstool_file = False,
        generate_args = ["-GNinja"],
        targets = ["", "install"],
        **kwargs):
    cache_entries.update(default_cache_entries)
    cache_entries_debug = dict(cache_entries)
    cache_entries_debug.update(debug_cache_entries)

    pf = ""
    if copy_pdb:
        # TODO: Add iterator of the first list presented of these options;
        # static_libraries[.pdb], pdb_names, name[.pdb] files
        if pdb_name == "":
            pdb_name = name

        copy_command = "cp {cmake_files_dir}/{pdb_name}.dir/{pdb_name}.pdb $INSTALLDIR/lib/{pdb_name}.pdb".format(cmake_files_dir = cmake_files_dir, pdb_name = pdb_name)
        if postfix_script != "":
            copy_command = copy_command + " && " + postfix_script

        pf = select({
            "@envoy//bazel:windows_dbg_build": copy_command,
            "//conditions:default": postfix_script,
        })
    else:
        pf = postfix_script

    cmake(
        name = name,
        cache_entries = select({
            "@envoy//bazel:dbg_build": cache_entries_debug,
            "//conditions:default": cache_entries,
        }),
        generate_args = generate_args,
        targets = targets,
        # TODO: Remove install target and make this work
        install = False,
        # TODO(lizan): Make this always true
        generate_crosstool_file = select({
            "@envoy//bazel:windows_x86_64": True,
            "//conditions:default": generate_crosstool_file,
        }),
        lib_source = lib_source,
        postfix_script = pf,
        **kwargs
    )

# Used to select a dependency that has different implementations on POSIX vs Windows.
# The platform-specific implementations should be specified with envoy_cc_posix_library
# and envoy_cc_win32_library respectively
def envoy_cc_platform_dep(name):
    return select({
        "@envoy//bazel:windows_x86_64": [name + "_win32"],
        "//conditions:default": [name + "_posix"],
    })

# Used to select a dependency that has different implementations on Linux vs rest of POSIX vs Windows.
# The platform-specific implementations should be specified with envoy_cc_linux_library,
# envoy_cc_posix_without_library and envoy_cc_win32_library respectively
def envoy_cc_platform_specific_dep(name):
    return select({
        "@envoy//bazel:windows_x86_64": [name + "_win32"],
        "@envoy//bazel:linux": [name + "_linux"],
        "//conditions:default": [name + "_posix"],
    })

# Envoy proto descriptor targets should be specified with this function.
# This is used for testing only.
def envoy_proto_descriptor(name, out, srcs = [], external_deps = []):
    input_files = ["$(location " + src + ")" for src in srcs]
    include_paths = [".", native.package_name()]

    if "api_httpbody_protos" in external_deps:
        srcs.append("@com_google_googleapis//google/api:httpbody.proto")
        include_paths.append("external/com_google_googleapis")

    if "http_api_protos" in external_deps:
        srcs.append("@com_google_googleapis//google/api:annotations.proto")
        srcs.append("@com_google_googleapis//google/api:http.proto")
        include_paths.append("external/com_google_googleapis")

    if "well_known_protos" in external_deps:
        srcs.append("@com_google_protobuf//:well_known_type_protos")
        srcs.append("@com_google_protobuf//:descriptor_proto_srcs")
        include_paths.append("external/com_google_protobuf/src")

    options = ["--include_imports"]
    options.extend(["-I" + include_path for include_path in include_paths])
    options.append("--descriptor_set_out=$@")

    cmd = "$(location //external:protoc) " + " ".join(options + input_files)
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [out],
        cmd = cmd,
        tools = ["//external:protoc"],
    )

# Dependencies on Google grpc should be wrapped with this function.
def envoy_google_grpc_external_deps():
    return envoy_select_google_grpc([envoy_external_dep_path("grpc")])

# Here we create wrappers for each of the public targets within the separate bazel
# files loaded above. This maintains envoy_build_system.bzl as the preferred import
# for BUILD files that need these build macros. Do not use the imports directly
# from the other bzl files (e.g. envoy_select.bzl, envoy_binary.bzl, etc.)

# Select wrappers (from envoy_select.bzl)
envoy_select_admin_html = _envoy_select_admin_html
envoy_select_admin_no_html = _envoy_select_admin_no_html
envoy_select_admin_functionality = _envoy_select_admin_functionality
envoy_select_static_extension_registration = _envoy_select_static_extension_registration
envoy_select_envoy_mobile_listener = _envoy_select_envoy_mobile_listener
envoy_select_envoy_mobile_xds = _envoy_select_envoy_mobile_xds
envoy_select_boringssl = _envoy_select_boringssl
envoy_select_disable_logging = _envoy_select_disable_logging
envoy_select_google_grpc = _envoy_select_google_grpc
envoy_select_enable_http3 = _envoy_select_enable_http3
envoy_select_enable_yaml = _envoy_select_enable_yaml
envoy_select_disable_exceptions = _envoy_select_disable_exceptions
envoy_select_hot_restart = _envoy_select_hot_restart
envoy_select_enable_http_datagrams = _envoy_select_enable_http_datagrams
envoy_select_signal_trace = _envoy_select_signal_trace
envoy_select_wasm_cpp_tests = _envoy_select_wasm_cpp_tests
envoy_select_wasm_rust_tests = _envoy_select_wasm_rust_tests
envoy_select_wasm_v8 = _envoy_select_wasm_v8
envoy_select_wasm_wamr = _envoy_select_wasm_wamr
envoy_select_wasm_wavm = _envoy_select_wasm_wavm
envoy_select_wasm_wasmtime = _envoy_select_wasm_wasmtime

# Binary wrappers (from envoy_binary.bzl)
envoy_cc_binary = _envoy_cc_binary

# Library wrappers (from envoy_library.bzl)
envoy_basic_cc_library = _envoy_basic_cc_library
envoy_cc_extension = _envoy_cc_extension
envoy_cc_contrib_extension = _envoy_cc_contrib_extension
envoy_cc_library = _envoy_cc_library
envoy_cc_linux_library = _envoy_cc_linux_library
envoy_cc_posix_library = _envoy_cc_posix_library
envoy_cc_posix_without_linux_library = _envoy_cc_posix_without_linux_library
envoy_cc_win32_library = _envoy_cc_win32_library
envoy_proto_library = _envoy_proto_library
envoy_pch_library = _envoy_pch_library

# Test wrappers (from envoy_test.bzl)
envoy_cc_fuzz_test = _envoy_cc_fuzz_test
envoy_cc_mock = _envoy_cc_mock
envoy_cc_test = _envoy_cc_test
envoy_cc_test_binary = _envoy_cc_test_binary
envoy_cc_test_library = _envoy_cc_test_library
envoy_cc_benchmark_binary = _envoy_cc_benchmark_binary
envoy_benchmark_test = _envoy_benchmark_test
envoy_py_test = _envoy_py_test
envoy_py_test_binary = _envoy_py_test_binary
envoy_sh_test = _envoy_sh_test

# Envoy Mobile defines (from envoy_mobile_defines.bz)
envoy_mobile_defines = _envoy_mobile_defines
