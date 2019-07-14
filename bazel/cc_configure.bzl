load("@bazel_tools//tools/cpp:cc_configure.bzl", _upstream_cc_autoconf_impl = "cc_autoconf_impl")
load("@bazel_tools//tools/cpp:lib_cc_configure.bzl", "get_cpu_value")
load("@bazel_tools//tools/cpp:unix_cc_configure.bzl", "find_cc")

def _build_envoy_cc_wrapper(repository_ctx):
    real_cc = find_cc(repository_ctx, {})

    # Copy our CC wrapper script into @local_config_cc, with the true paths
    # to the C and C++ compiler injected in. The wrapper will use these paths
    # to invoke the compiler after deciding which one is correct for the current
    # invocation.
    #
    # Since the script is Python, we can inject values using `repr(str(value))`
    # and escaping will be handled correctly.
    repository_ctx.template("extra_tools/envoy_cc_wrapper", repository_ctx.attr._envoy_cc_wrapper, {
        "{ENVOY_REAL_CC}": repr(str(real_cc)),
        "{ENVOY_CFLAGS}": repr(str(repository_ctx.os.environ.get("CFLAGS", ""))),
        "{ENVOY_CXXFLAGS}": repr(str(repository_ctx.os.environ.get("CXXFLAGS", ""))),
    })
    return repository_ctx.path("extra_tools/envoy_cc_wrapper")

def _needs_envoy_cc_wrapper(repository_ctx):
    # When building for Linux we set additional C++ compiler options that aren't
    # handled well by Bazel, so we need a wrapper around $CC to fix its
    # compiler invocations.
    cpu_value = get_cpu_value(repository_ctx)
    return cpu_value not in ["freebsd", "x64_windows", "darwin"]

def cc_autoconf_impl(repository_ctx):
    overriden_tools = {}
    if _needs_envoy_cc_wrapper(repository_ctx):
        # Bazel uses "gcc" as a generic name for all C and C++ compilers.
        overriden_tools["gcc"] = _build_envoy_cc_wrapper(repository_ctx)
    return _upstream_cc_autoconf_impl(repository_ctx, overriden_tools = overriden_tools)

cc_autoconf = repository_rule(
    implementation = cc_autoconf_impl,
    attrs = {
        "_envoy_cc_wrapper": attr.label(default = "@envoy//bazel:cc_wrapper.py"),
    },
    environ = [
        "ABI_LIBC_VERSION",
        "ABI_VERSION",
        "BAZEL_COMPILER",
        "BAZEL_HOST_SYSTEM",
        "BAZEL_CXXOPTS",
        "BAZEL_LINKOPTS",
        "BAZEL_PYTHON",
        "BAZEL_SH",
        "BAZEL_TARGET_CPU",
        "BAZEL_TARGET_LIBC",
        "BAZEL_TARGET_SYSTEM",
        "BAZEL_USE_CPP_ONLY_TOOLCHAIN",
        "BAZEL_DO_NOT_DETECT_CPP_TOOLCHAIN",
        "BAZEL_USE_LLVM_NATIVE_COVERAGE",
        "BAZEL_VC",
        "BAZEL_VS",
        "BAZEL_LLVM",
        "USE_CLANG_CL",
        "CC",
        "CFLAGS",
        "CXXFLAGS",
        "CC_CONFIGURE_DEBUG",
        "CC_TOOLCHAIN_NAME",
        "CPLUS_INCLUDE_PATH",
        "GCOV",
        "HOMEBREW_RUBY_PATH",
        "SYSTEMROOT",
        "VS90COMNTOOLS",
        "VS100COMNTOOLS",
        "VS110COMNTOOLS",
        "VS120COMNTOOLS",
        "VS140COMNTOOLS",
    ],
)

def cc_configure():
    cc_autoconf(name = "local_config_cc")
    native.bind(name = "cc_toolchain", actual = "@local_config_cc//:toolchain")
