load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "CONTRIB_EXTENSION_PACKAGE_VISIBILITY",
    "EXTENSION_PACKAGE_VISIBILITY",
)

# The main Envoy bazel file. Load this file for all Envoy-specific build macros
# and rules that you'd like to use in your BUILD files.
load("@rules_foreign_cc//foreign_cc:cmake.bzl", "cmake")
load(":envoy_binary.bzl", _envoy_cc_binary = "envoy_cc_binary")
load(":envoy_internal.bzl", "envoy_external_dep_path", _envoy_linkstatic = "envoy_linkstatic")
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
load(
    ":envoy_mobile_defines.bzl",
    _envoy_mobile_defines = "envoy_mobile_defines",
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
    _envoy_select_enable_exceptions = "envoy_select_enable_exceptions",
    _envoy_select_enable_http3 = "envoy_select_enable_http3",
    _envoy_select_enable_http_datagrams = "envoy_select_enable_http_datagrams",
    _envoy_select_enable_yaml = "envoy_select_enable_yaml",
    _envoy_select_envoy_mobile_listener = "envoy_select_envoy_mobile_listener",
    _envoy_select_envoy_mobile_xds = "envoy_select_envoy_mobile_xds",
    _envoy_select_google_grpc = "envoy_select_google_grpc",
    _envoy_select_hot_restart = "envoy_select_hot_restart",
    _envoy_select_nghttp2 = "envoy_select_nghttp2",
    _envoy_select_signal_trace = "envoy_select_signal_trace",
    _envoy_select_static_extension_registration = "envoy_select_static_extension_registration",
    _envoy_select_wasm_cpp_tests = "envoy_select_wasm_cpp_tests",
    _envoy_select_wasm_rust_tests = "envoy_select_wasm_rust_tests",
    _envoy_select_wasm_v8 = "envoy_select_wasm_v8",
    _envoy_select_wasm_wamr = "envoy_select_wasm_wamr",
    _envoy_select_wasm_wasmtime = "envoy_select_wasm_wasmtime",
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
        use_default_shell_env = True,
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
    # If cache_entries is a dict, merge defaults and wrap for debug builds.
    # If it's a select(), pass it through directly.
    if hasattr(cache_entries, "update"):
        cache_entries.update(default_cache_entries)
        cache_entries_debug = dict(cache_entries)
        cache_entries_debug.update(debug_cache_entries)
        final_cache_entries = select({
            "@envoy//bazel:dbg_build": cache_entries_debug,
            "//conditions:default": cache_entries,
        })
    else:
        final_cache_entries = cache_entries

    pf = ""
    if copy_pdb:
        # TODO: Add iterator of the first list presented of these options;
        # static_libraries[.pdb], pdb_names, name[.pdb] files
        if pdb_name == "":
            pdb_name = name

        copy_command = "cp {cmake_files_dir}/{pdb_name}.dir/{pdb_name}.pdb $$INSTALLDIR/lib/{pdb_name}.pdb".format(cmake_files_dir = cmake_files_dir, pdb_name = pdb_name)
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
        cache_entries = final_cache_entries,
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

    # TODO(phlax): Cleanup once bzlmod migration is complete
    has_protobuf_deps = False
    has_googleapis_deps = False
    protobuf_include_marker = None
    googleapis_include_marker = None

    if "api_httpbody_protos" in external_deps:
        srcs.append("@com_google_googleapis//google/api:httpbody.proto")
        has_googleapis_deps = True
        googleapis_include_marker = "@com_google_googleapis//google/api:httpbody.proto"

    if "http_api_protos" in external_deps:
        srcs.append("@com_google_googleapis//google/api:annotations.proto")
        srcs.append("@com_google_googleapis//google/api:http.proto")
        has_googleapis_deps = True
        if not googleapis_include_marker:
            googleapis_include_marker = "@com_google_googleapis//google/api:annotations.proto"

    if "well_known_protos" in external_deps:
        srcs.append("@com_google_protobuf//src/google/protobuf:any.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:api.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:descriptor.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:duration.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:empty.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:field_mask.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:source_context.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:struct.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:timestamp.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:type.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf:wrappers.proto")
        srcs.append("@com_google_protobuf//src/google/protobuf/compiler:plugin.proto")
        has_protobuf_deps = True
        protobuf_include_marker = "@com_google_protobuf//src/google/protobuf:any.proto"

    options = ["--include_imports"]

    # Build the command that computes include paths dynamically at execution time
    if has_protobuf_deps or has_googleapis_deps:
        cmd_parts = []
        if has_googleapis_deps and googleapis_include_marker:
            cmd_parts.append("GOOGLEAPIS_PROTO_PATH=$(location %s)" % googleapis_include_marker)
            cmd_parts.append("GOOGLEAPIS_INCLUDE_PATH=$${GOOGLEAPIS_PROTO_PATH%/google/api/*}")
        if has_protobuf_deps and protobuf_include_marker:
            cmd_parts.append("ANY_PROTO_PATH=$(location %s)" % protobuf_include_marker)
            cmd_parts.append("PROTOBUF_INCLUDE_PATH=$${ANY_PROTO_PATH%/google/protobuf/*}")
        cmd_parts.append("INCLUDE_OPTS=\"--include_imports\"")
        for include_path in include_paths:
            cmd_parts.append("INCLUDE_OPTS=\"$$INCLUDE_OPTS -I%s\"" % include_path)
        if has_googleapis_deps:
            cmd_parts.append("INCLUDE_OPTS=\"$$INCLUDE_OPTS -I$$GOOGLEAPIS_INCLUDE_PATH\"")
        if has_protobuf_deps:
            cmd_parts.append("INCLUDE_OPTS=\"$$INCLUDE_OPTS -I$$PROTOBUF_INCLUDE_PATH\"")
        cmd_parts.append("$(location @com_google_protobuf//:protoc) $$INCLUDE_OPTS --descriptor_set_out=$@ %s" % " ".join(input_files))
        cmd = " && ".join(cmd_parts)
    else:
        options.extend(["-I" + include_path for include_path in include_paths])
        options.append("--descriptor_set_out=$@")
        cmd = "$(location @com_google_protobuf//:protoc) " + " ".join(options + input_files)
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [out],
        cmd = cmd,
        tools = ["@com_google_protobuf//:protoc"],
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
envoy_select_enable_exceptions = _envoy_select_enable_exceptions
envoy_select_hot_restart = _envoy_select_hot_restart
envoy_select_nghttp2 = _envoy_select_nghttp2
envoy_select_enable_http_datagrams = _envoy_select_enable_http_datagrams
envoy_select_signal_trace = _envoy_select_signal_trace
envoy_select_wasm_cpp_tests = _envoy_select_wasm_cpp_tests
envoy_select_wasm_rust_tests = _envoy_select_wasm_rust_tests
envoy_select_wasm_v8 = _envoy_select_wasm_v8
envoy_select_wasm_wamr = _envoy_select_wasm_wamr
envoy_select_wasm_wasmtime = _envoy_select_wasm_wasmtime
envoy_select_linkstatic = _envoy_linkstatic

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
