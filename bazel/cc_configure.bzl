# This file was imported from https://github.com/bazelbuild/bazel at 3d00b2a. We apply a number of
# local modifications to deal with known issues in Bazel 0.10.0.
#
# * https://github.com/bazelbuild/bazel/issues/2840
# * (and potentially) https://github.com/bazelbuild/bazel/issues/2805
#
# See cc_configure.bzl.diff for the changes made in this fork.

# Copyright 2016 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Rules for configuring the C++ toolchain (experimental)."""


load("@bazel_tools//tools/cpp:windows_cc_configure.bzl", "configure_windows_toolchain")
load("@bazel_tools//tools/cpp:osx_cc_configure.bzl", "configure_osx_toolchain")
load("//bazel:unix_cc_configure.bzl", "configure_unix_toolchain")
load("@bazel_tools//tools/cpp:lib_cc_configure.bzl", "get_cpu_value")

def cc_autoconf_impl(repository_ctx, overriden_tools = dict()):
  repository_ctx.symlink(
      Label("@bazel_tools//tools/cpp:dummy_toolchain.bzl"), "dummy_toolchain.bzl")
  env = repository_ctx.os.environ
  cpu_value = get_cpu_value(repository_ctx)
  if cpu_value == "freebsd":
    # This is defaulting to the static crosstool, we should eventually
    # autoconfigure this platform too.  Theorically, FreeBSD should be
    # straightforward to add but we cannot run it in a docker container so
    # skipping until we have proper tests for FreeBSD.
    repository_ctx.symlink(Label("@bazel_tools//tools/cpp:CROSSTOOL"), "CROSSTOOL")
    repository_ctx.symlink(Label("@bazel_tools//tools/cpp:BUILD.static"), "BUILD")
  elif cpu_value == "x64_windows":
    # TODO(ibiryukov): overriden_tools are only supported in configure_unix_toolchain.
    # We might want to add that to Windows too(at least for msys toolchain).
    configure_windows_toolchain(repository_ctx)
  elif (cpu_value == "darwin" and
      ("BAZEL_USE_CPP_ONLY_TOOLCHAIN" not in env or env["BAZEL_USE_CPP_ONLY_TOOLCHAIN"] != "1")):
    configure_osx_toolchain(repository_ctx, overriden_tools)
  else:
    configure_unix_toolchain(repository_ctx, cpu_value, overriden_tools)

cc_autoconf = repository_rule(
    implementation = cc_autoconf_impl,
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
  """A C++ configuration rules that generate the crosstool file."""
  cc_autoconf(name="local_config_cc")
  native.bind(name="cc_toolchain", actual="@local_config_cc//:toolchain")
