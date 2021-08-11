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

# Selects the given values if http3 is enabled in the current build.
def envoy_select_enable_http3(xs, repository = ""):
    return select({
        repository + "//bazel:disable_http3": [],
        "//conditions:default": xs,
    })

# Selects the given values if hot restart is enabled in the current build.
def envoy_select_hot_restart(xs, repository = ""):
    return select({
        repository + "//bazel:disable_hot_restart": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm C++ SDK on the current platform.
def envoy_select_wasm_cpp_tests(xs):
    return select({
        "@envoy//bazel:darwin_arm64": [],
        "@envoy//bazel:linux_aarch64": [],
        "@envoy//bazel:wasm_none": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm Rust SDK on the current platform.
def envoy_select_wasm_rust_tests(xs):
    return select({
        "@envoy//bazel:wasm_none": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_v8(xs):
    return select({
        "@envoy//bazel:wasm_wamr": [],
        "@envoy//bazel:wasm_wasmtime": [],
        "@envoy//bazel:wasm_wavm": [],
        "@envoy//bazel:wasm_none": [],
        "//conditions:default": xs,
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
