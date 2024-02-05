# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy select targets. This is in a separate file to avoid a circular
# dependency with envoy_build_system.bzl.

# Used to select a dependency that has different implementations on POSIX vs Windows.
# The platform-specific implementations should be specified with envoy_cc_posix_library
# and envoy_cc_win32_library respectively
def envoy_cc_platform_dep(name):
    return select({
        "@envoy//bazel:windows_x86_64": [name + "_win32"],
        "//conditions:default": [name + "_posix"],
    })

def envoy_select_boringssl(if_fips, default = None, if_disabled = None):
    return select({
        "@envoy//bazel:boringssl_fips": if_fips,
        "@envoy//bazel:boringssl_disabled": if_disabled or [],
        "//conditions:default": default or [],
    })

# Selects the given values if Google gRPC is enabled in the current build.
def envoy_select_google_grpc(xs, repository = ""):
    return select({
        repository + "//bazel:disable_google_grpc": [],
        "//conditions:default": xs,
    })

# Selects the given values if logging is enabled in the current build.
def envoy_select_disable_logging(xs, repository = ""):
    return select({
        repository + "//bazel:disable_logging": xs,
        "//conditions:default": [],
    })

# Selects the given values if admin HTML is enabled in the current build.
def envoy_select_admin_html(xs, repository = ""):
    return select({
        repository + "//bazel:disable_admin_html": [],
        "//conditions:default": xs,
    })

# Selects the given values if admin functionality is enabled in the current build.
def envoy_select_admin_functionality(xs, repository = ""):
    return select({
        repository + "//bazel:disable_admin_functionality": [],
        "//conditions:default": xs,
    })

def envoy_select_admin_no_html(xs, repository = ""):
    return select({
        repository + "//bazel:disable_admin_html": xs,
        "//conditions:default": [],
    })

# Selects the given values if static extension registration is enabled in the current build.
def envoy_select_static_extension_registration(xs, repository = ""):
    return select({
        repository + "//bazel:disable_static_extension_registration": [],
        "//conditions:default": xs,
    })

# Selects the given values if the Envoy Mobile listener is enabled in the current build.
def envoy_select_envoy_mobile_listener(xs, repository = ""):
    return select({
        repository + "//bazel:disable_envoy_mobile_listener": [],
        "//conditions:default": xs,
    })

# Selects the given values if Envoy Mobile xDS is enabled in the current build.
def envoy_select_envoy_mobile_xds(xs, repository = ""):
    return select({
        repository + "//bazel:disable_envoy_mobile_xds": [],
        "//conditions:default": xs,
    })

# Selects the given values if http3 is enabled in the current build.
def envoy_select_enable_http3(xs, repository = ""):
    return select({
        repository + "//bazel:disable_http3": [],
        "//conditions:default": xs,
    })

# Selects the given values if yaml is enabled in the current build.
def envoy_select_enable_yaml(xs, repository = ""):
    return select({
        repository + "//bazel:disable_yaml": [],
        "//conditions:default": xs,
    })

# Selects the given values if exceptions are disabled in the current build.
def envoy_select_disable_exceptions(xs, repository = ""):
    return select({
        repository + "//bazel:disable_exceptions": xs,
        "//conditions:default": [],
    })

# Selects the given values if HTTP datagram support is enabled in the current build.
def envoy_select_enable_http_datagrams(xs, repository = ""):
    return select({
        repository + "//bazel:disable_http_datagrams": [],
        "//conditions:default": xs,
    })

# Selects the given values if hot restart is enabled in the current build.
def envoy_select_hot_restart(xs, repository = ""):
    return select({
        repository + "//bazel:disable_hot_restart": [],
        "//conditions:default": xs,
    })

# Selects the given values if full protos are enabled in the current build.
def envoy_select_enable_full_protos(xs, repository = ""):
    return select({
        repository + "//bazel:disable_full_protos": [],
        "//conditions:default": xs,
    })

# Selects the given values if lite protos are enabled in the current build.
def envoy_select_enable_lite_protos(xs, repository = ""):
    return select({
        repository + "//bazel:disable_full_protos": xs,
        "//conditions:default": [],
    })

# Selects the given values if signal trace is enabled in the current build.
def envoy_select_signal_trace(xs, repository = ""):
    return select({
        repository + "//bazel:disable_signal_trace": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm C++ SDK on the current platform.
def envoy_select_wasm_cpp_tests(xs):
    return select({
        "@envoy//bazel:not_x86_or_wasm_disabled": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm Rust SDK on the current platform.
def envoy_select_wasm_rust_tests(xs):
    return select({
        "@envoy//bazel:wasm_disabled": [],
        # TODO(phlax): re-enable once issues with llvm profiler are resolved
        #   (see https://github.com/envoyproxy/envoy/issues/24164)
        "@envoy//bazel:coverage_build": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_v8(xs):
    return select({
        "@envoy//bazel:wasm_v8": xs,
        "@envoy//bazel:wasm_wamr": [],
        "@envoy//bazel:wasm_wasmtime": [],
        "@envoy//bazel:wasm_wavm": [],
        "@envoy//bazel:wasm_disabled": [],
        # TODO(phlax): re-enable once issues with llvm profiler are resolved
        #   (see https://github.com/envoyproxy/envoy/issues/24164)
        "@envoy//bazel:coverage_build": [],
        "//conditions:default": xs,  # implicit default (v8)
    })

# Selects True or False depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_v8_bool():
    return select({
        "@envoy//bazel:wasm_v8": True,
        "@envoy//bazel:wasm_wamr": False,
        "@envoy//bazel:wasm_wasmtime": False,
        "@envoy//bazel:wasm_wavm": False,
        "@envoy//bazel:wasm_disabled": False,
        # TODO(phlax): re-enable once issues with llvm profiler are resolved
        #   (see https://github.com/envoyproxy/envoy/issues/24164)
        "@envoy//bazel:coverage_build": False,
        "//conditions:default": True,  # implicit default (v8)
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_wamr(xs):
    return select({
        "@envoy//bazel:wasm_wamr": xs,
        "//conditions:default": [],
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_wavm(xs):
    return select({
        "@envoy//bazel:wasm_wavm": xs,
        "//conditions:default": [],
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_wasmtime(xs):
    return select({
        "@envoy//bazel:wasm_wasmtime": xs,
        "//conditions:default": [],
    })
