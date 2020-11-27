load("@io_bazel_rules_rust//rust:rust.bzl", "rust_binary")
load("@rules_cc//cc:defs.bzl", "cc_binary")

def _wasm_cc_transition_impl(settings, attr):
    return {
        "//command_line_option:cpu": "wasm32",
        "//command_line_option:crosstool_top": "@proxy_wasm_cpp_sdk//toolchain:emscripten",

        # Overriding copt/cxxopt/linkopt to prevent sanitizers/coverage options leak
        # into WASM build configuration
        "//command_line_option:copt": [],
        "//command_line_option:cxxopt": [],
        "//command_line_option:linkopt": [],
        "//command_line_option:collect_code_coverage": "false",
        "//command_line_option:fission": "no",
    }

def _wasm_rust_transition_impl(settings, attr):
    return {
        "//command_line_option:platforms": "@io_bazel_rules_rust//rust/platform:wasm",
    }

wasm_cc_transition = transition(
    implementation = _wasm_cc_transition_impl,
    inputs = [],
    outputs = [
        "//command_line_option:cpu",
        "//command_line_option:crosstool_top",
        "//command_line_option:copt",
        "//command_line_option:cxxopt",
        "//command_line_option:fission",
        "//command_line_option:linkopt",
        "//command_line_option:collect_code_coverage",
    ],
)

wasm_rust_transition = transition(
    implementation = _wasm_rust_transition_impl,
    inputs = [],
    outputs = [
        "//command_line_option:platforms",
    ],
)

def _wasm_binary_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    if ctx.attr.precompile:
        ctx.actions.run(
            executable = ctx.executable._compile_tool,
            arguments = [ctx.files.binary[0].path, out.path],
            outputs = [out],
            inputs = ctx.files.binary,
        )
    else:
        ctx.actions.run(
            executable = "cp",
            arguments = [ctx.files.binary[0].path, out.path],
            outputs = [out],
            inputs = ctx.files.binary,
        )

    return [DefaultInfo(files = depset([out]), runfiles = ctx.runfiles([out]))]

def _wasm_attrs(transition):
    return {
        "binary": attr.label(mandatory = True, cfg = transition),
        "precompile": attr.bool(default = False),
        # This is deliberately in target configuration to avoid compiling v8 twice.
        "_compile_tool": attr.label(default = "@envoy//test/tools/wee8_compile:wee8_compile_tool", executable = True, cfg = "target"),
        "_whitelist_function_transition": attr.label(default = "@bazel_tools//tools/whitelists/function_transition_whitelist"),
    }

# WASM binary rule implementation.
# This copies the binary specified in binary attribute in WASM configuration to
# target configuration, so a binary in non-WASM configuration can depend on them.
wasm_cc_binary_rule = rule(
    implementation = _wasm_binary_impl,
    attrs = _wasm_attrs(wasm_cc_transition),
)

wasm_rust_binary_rule = rule(
    implementation = _wasm_binary_impl,
    attrs = _wasm_attrs(wasm_rust_transition),
)

def wasm_cc_binary(name, tags = [], repository = "", **kwargs):
    wasm_name = "_wasm_" + name
    kwargs.setdefault("additional_linker_inputs", ["@proxy_wasm_cpp_sdk//:jslib", "@envoy//source/extensions/common/wasm/ext:jslib"])

    if repository == "@envoy":
        envoy_js = "--js-library external/envoy/source/extensions/common/wasm/ext/envoy_wasm_intrinsics.js"
    else:
        envoy_js = "--js-library source/extensions/common/wasm/ext/envoy_wasm_intrinsics.js"
    kwargs.setdefault("linkopts", [
        envoy_js,
        "--js-library external/proxy_wasm_cpp_sdk/proxy_wasm_intrinsics.js",
    ])
    kwargs.setdefault("visibility", ["//visibility:public"])
    cc_binary(
        name = wasm_name,
        # Adding manual tag it won't be built in non-WASM (e.g. x86_64 config)
        # when an wildcard is specified, but it will be built in WASM configuration
        # when the wasm_binary below is built.
        tags = ["manual"],
        **kwargs
    )

    wasm_cc_binary_rule(
        name = name,
        binary = ":" + wasm_name,
        tags = tags + ["manual"],
    )

def envoy_wasm_cc_binary(name, tags = [], **kwargs):
    wasm_cc_binary(name, tags, repository = "", **kwargs)

def wasm_rust_binary(name, tags = [], **kwargs):
    wasm_name = "_wasm_" + name.replace(".", "_")
    kwargs.setdefault("visibility", ["//visibility:public"])

    rust_binary(
        name = wasm_name,
        edition = "2018",
        crate_type = "cdylib",
        out_binary = True,
        tags = ["manual"],
        **kwargs
    )

    wasm_rust_binary_rule(
        name = name,
        precompile = select({
            "@envoy//bazel:linux_x86_64": True,
            "//conditions:default": False,
        }),
        binary = ":" + wasm_name,
        tags = tags + ["manual"],
    )
