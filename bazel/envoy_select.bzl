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

# Selects the given values if hot restart is enabled in the current build.
def envoy_select_hot_restart(xs, repository = ""):
    return select({
        repository + "//bazel:disable_hot_restart_or_apple": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the WASM runtimes enabled in the current build.
def envoy_select_wasm(xs):
    return select({
        "@envoy//bazel:wasm_none": [],
        "//conditions:default": xs,
    })

def envoy_select_wasm_v8(xs):
    return select({
        "@envoy//bazel:wasm_wavm": [],
        "@envoy//bazel:wasm_none": [],
        "//conditions:default": xs,
    })

def envoy_select_wasm_wavm(xs):
    return select({
        "@envoy//bazel:wasm_all": xs,
        "@envoy//bazel:wasm_wavm": xs,
        "//conditions:default": [],
    })

def envoy_select_wasm_all_v8_wavm_none(xs1, xs2, xs3, xs4):
    return select({
        "@envoy//bazel:wasm_all": xs1,
        "@envoy//bazel:wasm_v8": xs2,
        "@envoy//bazel:wasm_wavm": xs3,
        "@envoy//bazel:wasm_none": xs4,
        "//conditions:default": xs2,
    })

# Select the given values if use legacy codecs in test is on in the current build.
def envoy_select_new_codecs_in_integration_tests(xs, repository = ""):
    return select({
        repository + "//bazel:enable_new_codecs_in_integration_tests": xs,
        "//conditions:default": [],
    })
