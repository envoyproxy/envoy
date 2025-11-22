# DEPRECATED: This file is no longer used for bzlmod builds.
# Python dependencies are now handled by upstream rules_python extensions in MODULE.bazel.
# 
# For bzlmod builds:
# - pip dependencies are handled by @rules_python//python/extensions:pip.bzl
# - python toolchains are handled by @rules_python//python/extensions:python.bzl
#
# This file is preserved for WORKSPACE-only builds and will be removed once
# full bzlmod migration is complete.

# NOTE: system_python.bzl was removed in protobuf 30.0
# load("@com_google_protobuf//bazel:system_python.bzl", "system_python")
load("@envoy_toolshed//:packages.bzl", "load_packages")
load("@rules_python//python:pip.bzl", "pip_parse")

def envoy_python_dependencies():
    """DEPRECATED: Use upstream rules_python extensions in bzlmod instead."""
    # TODO(phlax): rename base_pip3 -> pip3 and remove this
    load_packages()
    pip_parse(
        name = "base_pip3",
        python_interpreter_target = "@python3_12_host//:python",
        requirements_lock = "@envoy//tools/base:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    pip_parse(
        name = "dev_pip3",
        python_interpreter_target = "@python3_12_host//:python",
        requirements_lock = "@envoy//tools/dev:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    pip_parse(
        name = "fuzzing_pip3",
        python_interpreter_target = "@python3_12_host//:python",
        requirements_lock = "@rules_fuzzing//fuzzing:requirements.txt",
        extra_pip_args = ["--require-hashes"],
    )

    # system_python() was removed in protobuf 30.0
    # system_python(
    #     name = "system_python",
    #     minimum_python_version = "3.7",
    # )
