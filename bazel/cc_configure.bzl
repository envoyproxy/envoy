load("@bazel_tools//tools/cpp:cc_configure.bzl", _upstream_cc_autoconf_impl = "cc_autoconf_impl")
load("@bazel_tools//tools/cpp:lib_cc_configure.bzl", "get_cpu_value")
load("@bazel_tools//tools/cpp:unix_cc_configure.bzl", "find_cc")

def _quiet_fake_which(program):
  return struct(_envoy_fake_which = program)

def _noisy_fake_which(program):
  return None

def _build_envoy_cc_wrapper(repository_ctx):
  real_cc = find_cc(repository_ctx, {})
  environ_cxx = repository_ctx.os.environ.get("CXX", "g++")
  fake_os = struct(
    environ = {"CC": environ_cxx},
  )
  real_cxx = find_cc(struct(
      which = _quiet_fake_which,
      os = fake_os,
  ), {})
  if hasattr(real_cxx, "_envoy_fake_which"):
    real_cxx = repository_ctx.which(real_cxx._envoy_fake_which)
    if real_cxx == None:
      find_cc(struct(
        which = _noisy_fake_which,
        os = fake_os,
      ), {})
  repository_ctx.template("envoy_cc_wrapper", repository_ctx.attr._envoy_cc_wrapper, {
      "{ENVOY_REAL_CC}": repr(str(real_cc)),
      "{ENVOY_REAL_CXX}": repr(str(real_cxx)),
  })
  return repository_ctx.path("envoy_cc_wrapper")

def cc_autoconf_impl(repository_ctx):
  cpu_value = get_cpu_value(repository_ctx)
  overriden_tools = {}
  if cpu_value not in ["freebsd", "x64_windows", "darwin"]:
    overriden_tools["gcc"] = _build_envoy_cc_wrapper(repository_ctx)
  return _upstream_cc_autoconf_impl(repository_ctx, overriden_tools=overriden_tools)

cc_autoconf = repository_rule(
    implementation = cc_autoconf_impl,
    attrs = {
        "_envoy_cc_wrapper": attr.label(default="@envoy//bazel:cc_wrapper.py"),
    },
    environ = [
        "ABI_LIBC_VERSION",
        "ABI_VERSION",
        "BAZEL_COMPILER",
        "BAZEL_HOST_SYSTEM",
        "BAZEL_PYTHON",
        "BAZEL_SH",
        "BAZEL_TARGET_CPU",
        "BAZEL_TARGET_LIBC",
        "BAZEL_TARGET_SYSTEM",
        "BAZEL_USE_CPP_ONLY_TOOLCHAIN",
        "BAZEL_VC",
        "BAZEL_VS",
        "CC",
        "CXX",
        "CC_CONFIGURE_DEBUG",
        "CC_TOOLCHAIN_NAME",
        "CPLUS_INCLUDE_PATH",
        "CUDA_COMPUTE_CAPABILITIES",
        "CUDA_PATH",
        "CXX",
        "HOMEBREW_RUBY_PATH",
        "NO_WHOLE_ARCHIVE_OPTION",
        "USE_DYNAMIC_CRT",
        "USE_MSVC_WRAPPER",
        "SYSTEMROOT",
        "VS90COMNTOOLS",
        "VS100COMNTOOLS",
        "VS110COMNTOOLS",
        "VS120COMNTOOLS",
        "VS140COMNTOOLS"])

def cc_configure():
  cc_autoconf(name="local_config_cc")
  native.bind(name="cc_toolchain", actual="@local_config_cc//:toolchain")
