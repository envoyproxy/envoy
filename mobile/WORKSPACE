workspace(name = "envoy_mobile")

load("@envoy_mobile//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")
envoy_mobile_repositories()

local_repository(
    name = "envoy",
    path = "envoy",
)

local_repository(
    name = "envoy_build_config",
    path = "envoy_build_config",
)

load("@envoy//bazel:api_binding.bzl", "envoy_api_binding")
envoy_api_binding()

load("@envoy//bazel:api_repositories.bzl", "envoy_api_dependencies")
envoy_api_dependencies()

load("@envoy//bazel:repositories.bzl", "envoy_dependencies")
envoy_dependencies()

load("@envoy//bazel:repositories_extra.bzl", "envoy_dependencies_extra")
envoy_dependencies_extra()

load("@envoy//bazel:dependency_imports.bzl", "envoy_dependency_imports")
envoy_dependency_imports()

load("@envoy_mobile//bazel:envoy_mobile_dependencies.bzl", "envoy_mobile_dependencies")
envoy_mobile_dependencies()

load("@envoy_mobile//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")
envoy_mobile_toolchains()

load("@pybind11_bazel//:python_configure.bzl", "python_configure")
python_configure(name = "local_config_python", python_version = "3")

load("//bazel:python.bzl", "declare_python_abi")
declare_python_abi(name = "python_abi", python_version = "3")

android_sdk_repository(name = "androidsdk", api_level = 30)
android_ndk_repository(name = "androidndk", api_level = 21)

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "bazel_toolchains",
    commit = "810ac3490df9113cfaa50a4ee3d204a29c81a24c",
    remote = "https://github.com/bazelbuild/bazel-toolchains.git",
)

load("@bazel_toolchains//rules:rbe_repo.bzl", "rbe_autoconfig")

rbe_autoconfig(
    name = "engflow_remote_config",
    digest = "sha256:b4fe088084579339ae8f7a44af899bbebd86a290af56e5ab7cc85ca99a09499c",
    registry = "docker.io",
    repository = "envoyproxy/envoy-build-ubuntu",
    use_legacy_platform_definition = False,
    create_java_configs = False,
    exec_properties = {
        "Pool": "linux",
    },
)

rbe_autoconfig(
    name = "engflow_remote_config_clang",
    digest = "sha256:b4fe088084579339ae8f7a44af899bbebd86a290af56e5ab7cc85ca99a09499c",
    registry = "docker.io",
    repository = "envoyproxy/envoy-build-ubuntu",
    use_legacy_platform_definition = False,
    env = {
        "CC": "/opt/llvm/bin/clang",
        "CXX": "/opt/llvm/bin/clang++",
    },
    exec_properties = {
        "Pool": "linux",
    },
    create_java_configs = False,
)

rbe_autoconfig(
    name = "engflow_remote_config_clang_asan",
    digest = "sha256:b4fe088084579339ae8f7a44af899bbebd86a290af56e5ab7cc85ca99a09499c",
    registry = "docker.io",
    repository = "envoyproxy/envoy-build-ubuntu",
    use_legacy_platform_definition = False,
    env = {
        "CC": "/opt/llvm/bin/clang",
        "CXX": "/opt/llvm/bin/clang++",
    },
    exec_properties = {
        "Pool": "linux",
        # Necessary to workaround https://github.com/google/sanitizers/issues/916, otherwise, dangling threads in the
        # docker container fail tests on teardown (example: https://github.com/envoyproxy/envoy-mobile/runs/3443649963)
        "dockerAddCapabilities": "SYS_PTRACE",
    },
)

rbe_autoconfig(
    name = "engflow_remote_config_clang_coverage",
    digest = "sha256:b4fe088084579339ae8f7a44af899bbebd86a290af56e5ab7cc85ca99a09499c",
    registry = "docker.io",
    repository = "envoyproxy/envoy-build-ubuntu",
    use_legacy_platform_definition = False,
    env = {
        "CC": "/opt/llvm/bin/clang",
        "CXX": "/opt/llvm/bin/clang++",
        "GCOV": "/opt/llvm/bin/llvm-profdata",
        "BAZEL_LLVM_COV": "/opt/llvm/bin/llvm-cov",
        "BAZEL_USE_LLVM_NATIVE_COVERAGE": "1",
    },
    exec_properties = {
        "Pool": "linux",
    },
    create_java_configs = False,
)
