# DO NOT LOAD THIS FILE. Load envoy_build_system.bzl instead.
# Envoy select targets. This is in a separate file to avoid a circular
# dependency with envoy_build_system.bzl.

_DISABLE_ADMIN_FUNCTIONALITY = Label("//bazel:disable_admin_functionality")
_DISABLE_ADMIN_HTML = Label("//bazel:disable_admin_html")
_DISABLE_EXCEPTIONS = Label("//bazel:disable_exceptions")
_DISABLE_ENVOY_MOBILE_LISTENER = Label("//bazel:disable_envoy_mobile_listener")
_DISABLE_ENVOY_MOBILE_XDS = Label("//bazel:disable_envoy_mobile_xds")
_DISABLE_FULL_PROTOS = Label("//bazel:disable_full_protos")
_DISABLE_GOOGLE_GRPC = Label("//bazel:disable_google_grpc")
_DISABLE_HOT_RESTART = Label("//bazel:disable_hot_restart")
_DISABLE_HTTP3 = Label("//bazel:disable_http3")
_DISABLE_HTTP_DATAGRAMS = Label("//bazel:disable_http_datagrams")
_DISABLE_LOGGING = Label("//bazel:disable_logging")
_DISABLE_NGHTTP2 = Label("//bazel:disable_nghttp2")
_DISABLE_SIGNAL_TRACE = Label("//bazel:disable_signal_trace")
_DISABLE_STATIC_EXTENSION_REGISTRATION = Label("//bazel:disable_static_extension_registration")
_DISABLE_YAML = Label("//bazel:disable_yaml")
_NOT_X86_OR_WASM_DISABLED = Label("//bazel:not_x86_or_wasm_disabled")
_WINDOWS_X86_64 = Label("//bazel:windows_x86_64")

def _validate_repository(repository):
    if repository not in ("", "@envoy"):
        fail(
            "The `repository` argument is deprecated and only accepts \"\" or \"@envoy\". " +
            "Use the `@envoy//bazel` label_flag overrides instead.",
        )

# Used to select a dependency that has different implementations on POSIX vs Windows.
# The platform-specific implementations should be specified with envoy_cc_posix_library
# and envoy_cc_win32_library respectively
def envoy_cc_platform_dep(name):
    return select({
        _WINDOWS_X86_64: [name + "_win32"],
        "//conditions:default": [name + "_posix"],
    })

# Selects the given values if Google gRPC is enabled in the current build.
def envoy_select_google_grpc(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_GOOGLE_GRPC: [],
        "//conditions:default": xs,
    })

# Selects the given values if logging is enabled in the current build.
def envoy_select_disable_logging(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_LOGGING: xs,
        "//conditions:default": [],
    })

# Selects the given values if admin HTML is enabled in the current build.
def envoy_select_admin_html(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_ADMIN_HTML: [],
        "//conditions:default": xs,
    })

# Selects the given values if admin functionality is enabled in the current build.
def envoy_select_admin_functionality(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_ADMIN_FUNCTIONALITY: [],
        "//conditions:default": xs,
    })

def envoy_select_admin_no_html(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_ADMIN_HTML: xs,
        "//conditions:default": [],
    })

# Selects the given values if static extension registration is enabled in the current build.
def envoy_select_static_extension_registration(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_STATIC_EXTENSION_REGISTRATION: [],
        "//conditions:default": xs,
    })

# Selects the given values if the Envoy Mobile listener is enabled in the current build.
def envoy_select_envoy_mobile_listener(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_ENVOY_MOBILE_LISTENER: [],
        "//conditions:default": xs,
    })

# Selects the given values if Envoy Mobile xDS is enabled in the current build.
def envoy_select_envoy_mobile_xds(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_ENVOY_MOBILE_XDS: [],
        "//conditions:default": xs,
    })

# Selects the given values if http3 is enabled in the current build.
def envoy_select_enable_http3(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_HTTP3: [],
        "//conditions:default": xs,
    })

# Selects the given values if yaml is enabled in the current build.
def envoy_select_enable_yaml(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_YAML: [],
        "//conditions:default": xs,
    })

# Selects the given values if exceptions are disabled in the current build.
def envoy_select_disable_exceptions(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_EXCEPTIONS: xs,
        "//conditions:default": [],
    })

# Selects the given values if exceptions are enabled in the current build.
def envoy_select_enable_exceptions(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_EXCEPTIONS: [],
        "//conditions:default": xs,
    })

# Selects the given values if HTTP datagram support is enabled in the current build.
def envoy_select_enable_http_datagrams(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_HTTP_DATAGRAMS: [],
        "//conditions:default": xs,
    })

# Selects the given values if hot restart is enabled in the current build.
def envoy_select_hot_restart(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_HOT_RESTART: [],
        "//conditions:default": xs,
    })

# Selects the given values if hot restart is enabled in the current build.
def envoy_select_nghttp2(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_NGHTTP2: [],
        "//conditions:default": xs,
    })

# Selects the given values if full protos are enabled in the current build.
def envoy_select_enable_full_protos(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_FULL_PROTOS: [],
        "//conditions:default": xs,
    })

# Selects the given values if lite protos are enabled in the current build.
def envoy_select_enable_lite_protos(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_FULL_PROTOS: xs,
        "//conditions:default": [],
    })

# Selects the given values if signal trace is enabled in the current build.
def envoy_select_signal_trace(xs, repository = ""):
    _validate_repository(repository)
    return select({
        _DISABLE_SIGNAL_TRACE: [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm C++ SDK on the current platform.
def envoy_select_wasm_cpp_tests(xs):
    return select({
        _NOT_X86_OR_WASM_DISABLED: [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build,
# and the ability to build tests using Proxy-Wasm Rust SDK on the current platform.
def envoy_select_wasm_rust_tests(xs):
    return select({
        "@proxy-wasm-cpp-host//bazel:engine_disabled": [],
        "//conditions:default": xs,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_v8(xs):
    return select({
        "@proxy-wasm-cpp-host//bazel:engine_wamr_interp": [],
        "@proxy-wasm-cpp-host//bazel:engine_wamr_jit": [],
        "@proxy-wasm-cpp-host//bazel:engine_wasmtime": [],
        "@proxy-wasm-cpp-host//bazel:engine_disabled": [],
        "//conditions:default": xs,
    })

# Selects True or False depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_v8_bool():
    return select({
        "@proxy-wasm-cpp-host//bazel:engine_wamr_interp": False,
        "@proxy-wasm-cpp-host//bazel:engine_wamr_jit": False,
        "@proxy-wasm-cpp-host//bazel:engine_wasmtime": False,
        "@proxy-wasm-cpp-host//bazel:engine_disabled": False,
        "//conditions:default": True,
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_wamr(xs):
    return select({
        "@proxy-wasm-cpp-host//bazel:engine_wamr_interp": xs,
        "@proxy-wasm-cpp-host//bazel:engine_wamr_jit": xs,
        "//conditions:default": [],
    })

# Selects the given values depending on the Wasm runtimes enabled in the current build.
def envoy_select_wasm_wasmtime(xs):
    return select({
        "@proxy-wasm-cpp-host//bazel:engine_wasmtime": xs,
        "//conditions:default": [],
    })
