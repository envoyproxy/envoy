load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")
load("@envoy//bazel/toolchains:configs/versions.bzl", _generated_toolchain_config_suite_autogen_spec = "TOOLCHAIN_CONFIG_AUTOGEN_SPEC")

_ENVOY_BUILD_IMAGE_REGISTRY = "gcr.io"
_ENVOY_BUILD_IMAGE_REPOSITORY = "envoy-ci/envoy-build"
_ENVOY_BUILD_IMAGE_DIGEST = "sha256:9dbe1cba2b3340d49a25a1d286c8d49083ec986a6fead27f487e80ca334f065f"
_ENVOY_BUILD_IMAGE_JAVA_HOME = "/usr/lib/jvm/java-8-openjdk-amd64"
_CONFIGS_OUTPUT_BASE = "bazel/toolchains/configs"

_CLANG_ENV = {
    "BAZEL_COMPILER": "clang",
    "BAZEL_LINKLIBS": "-l%:libstdc++.a",
    "BAZEL_LINKOPTS": "-lm:-static-libgcc",
    "BAZEL_USE_LLVM_NATIVE_COVERAGE": "1",
    "GCOV": "llvm-profdata",
    "CC": "clang",
    "CXX": "clang++",
    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/llvm-8/bin",
}

_GCC_ENV = {
    "BAZEL_COMPILER": "gcc",
    "BAZEL_LINKLIBS": "-l%:libstdc++.a",
    "BAZEL_LINKOPTS": "-lm:-static-libgcc",
    "CC": "gcc",
    "CXX": "g++",
    "PATH": "/usr/sbin:/usr/bin:/sbin:/bin:/usr/lib/llvm-8/bin",
}

_TOOLCHAIN_CONFIG_SUITE_SPEC = {
    "container_registry": _ENVOY_BUILD_IMAGE_REGISTRY,
    "container_repo": _ENVOY_BUILD_IMAGE_REPOSITORY,
    "output_base": _CONFIGS_OUTPUT_BASE,
    "repo_name": "envoy",
    "toolchain_config_suite_autogen_spec": _generated_toolchain_config_suite_autogen_spec,
}

def _rbe_toolchains_generator():
    rbe_autoconfig(
        name = "rbe_ubuntu_clang_gen",
        digest = _ENVOY_BUILD_IMAGE_DIGEST,
        export_configs = True,
        java_home = _ENVOY_BUILD_IMAGE_JAVA_HOME,
        registry = _ENVOY_BUILD_IMAGE_REGISTRY,
        repository = _ENVOY_BUILD_IMAGE_REPOSITORY,
        env = _CLANG_ENV,
        toolchain_config_spec_name = "clang",
        toolchain_config_suite_spec = _TOOLCHAIN_CONFIG_SUITE_SPEC,
        use_checked_in_confs = "False",
    )

    rbe_autoconfig(
        name = "rbe_ubuntu_gcc_gen",
        digest = _ENVOY_BUILD_IMAGE_DIGEST,
        export_configs = True,
        java_home = _ENVOY_BUILD_IMAGE_JAVA_HOME,
        registry = _ENVOY_BUILD_IMAGE_REGISTRY,
        repository = _ENVOY_BUILD_IMAGE_REPOSITORY,
        env = _GCC_ENV,
        toolchain_config_spec_name = "gcc",
        toolchain_config_suite_spec = _TOOLCHAIN_CONFIG_SUITE_SPEC,
        use_checked_in_confs = "False",
    )

def _generated_rbe_toolchains():
    rbe_autoconfig(
        name = "rbe_ubuntu_clang",
        digest = _ENVOY_BUILD_IMAGE_DIGEST,
        export_configs = True,
        java_home = _ENVOY_BUILD_IMAGE_JAVA_HOME,
        registry = _ENVOY_BUILD_IMAGE_REGISTRY,
        repository = _ENVOY_BUILD_IMAGE_REPOSITORY,
        toolchain_config_spec_name = "clang",
        toolchain_config_suite_spec = _TOOLCHAIN_CONFIG_SUITE_SPEC,
        use_checked_in_confs = "Force",
    )

def rbe_toolchains_config():
    _rbe_toolchains_generator()
    _generated_rbe_toolchains()
