# TODO(phlax): Move this to toolsheds toolchain alias
"""Module extensions for Envoy's non-module dependencies.

This file defines module extensions to support Envoy's bzlmod migration while
respecting existing WORKSPACE patches and custom BUILD files.
"""

load("@envoy_toolshed//compile:llvm_minimal.bzl", "render_llvm_repo_build")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load("//bazel/external/cargo/remote:crates.bzl", "crate_repositories")
load(":envoy_build_config.bzl", "default_envoy_build_config")
load(":repo.bzl", "envoy_repo")

_LOCKFILE_LABEL = Label("//:MODULE.bazel.lock")
_MODULES_SEGMENT = "/modules/"
_SOURCE_JSON_SUFFIX = "/source.json"
_LLVM_VERSION = "22.1.8"

_LLVM_LIBRARY_TARGETS = """

filegroup(
    name = "libclang_cpp",
    srcs = glob(["lib/**/libclang-cpp.so*"], allow_empty = True),
)

filegroup(
    name = "libllvm",
    srcs = glob(["lib/**/libLLVM.so*"], allow_empty = True),
)

cc_library(
    name = "clang_tooling_headers",
    hdrs = glob(["include/clang/**", "include/clang-c/**", "include/llvm/**", "include/llvm-c/**"], allow_empty = True),
    includes = ["include"],
)
"""

def _llvm_repo_build(llvm_version):
    build = render_llvm_repo_build(llvm_version.split(".")[0])
    libclang_glob = 'glob(["lib/libclang.so*", "lib/libclang*.dylib"], allow_empty = True)'
    if libclang_glob not in build:
        fail("The envoy_toolshed LLVM repository BUILD template changed")
    return build.replace(
        libclang_glob,
        'glob(["lib/**/libclang.so*", "lib/**/libclang*.dylib"], allow_empty = True)',
    ) + _LLVM_LIBRARY_TARGETS

def _host_llvm_repo_impl(repository_ctx):
    llvm_root = repository_ctx.path(repository_ctx.attr.path)
    for directory in ["bin", "include", "lib"]:
        path = llvm_root.get_child(directory)
        if not path.exists:
            fail("Host LLVM directory does not exist: %s" % path)
        _symlink_directory_contents(repository_ctx, path, directory)
    repository_ctx.file(
        "BUILD.bazel",
        _llvm_repo_build(repository_ctx.attr.llvm_version),
    )

_host_llvm_repo = repository_rule(
    implementation = _host_llvm_repo_impl,
    local = True,
    attrs = {
        "llvm_version": attr.string(mandatory = True),
        "path": attr.string(mandatory = True),
    },
)

def _symlink_directory_contents(repository_ctx, source, destination):
    for child in source.readdir():
        repository_ctx.symlink(child, destination + "/" + child.basename)

def _llvm_alias_repo_impl(repository_ctx):
    os_name = repository_ctx.os.name.lower()
    arch = repository_ctx.os.arch.lower()
    if os_name.startswith("linux") and (arch.startswith("x86_64") or arch.startswith("amd64")):
        llvm_root = repository_ctx.path(repository_ctx.attr.minimal_linux_x64).dirname
    elif os_name.startswith("linux") and (arch.startswith("aarch64") or arch.startswith("arm64")):
        llvm_root = repository_ctx.path(repository_ctx.attr.minimal_linux_arm64).dirname
    elif (os_name.startswith("mac os x") or os_name.startswith("darwin")) and (
        arch.startswith("aarch64") or arch.startswith("arm64")
    ):
        llvm_root = repository_ctx.path(repository_ctx.attr.minimal_macos_arm64).dirname
    else:
        fail(
            "Unsupported host platform for llvm_toolchain_llvm: %s %s" %
            (repository_ctx.os.name, repository_ctx.os.arch),
        )

    for directory in ["bin", "include", "lib"]:
        _symlink_directory_contents(repository_ctx, llvm_root.get_child(directory), directory)
    repository_ctx.file(
        "BUILD.bazel",
        _llvm_repo_build(_LLVM_VERSION),
    )

_llvm_alias_repo = repository_rule(
    implementation = _llvm_alias_repo_impl,
    attrs = {
        "minimal_linux_arm64": attr.label(mandatory = True),
        "minimal_linux_x64": attr.label(mandatory = True),
        "minimal_macos_arm64": attr.label(mandatory = True),
    },
)

def _envoy_llvm_impl(module_ctx):
    host = None
    for module in module_ctx.modules:
        for tag in module.tags.host:
            if not module.is_root:
                fail("envoy_llvm_extension.host may only be specified by the root module")
            if host != None:
                fail("envoy_llvm_extension.host may only be specified once")
            host = tag

    if host:
        _host_llvm_repo(
            name = "llvm_toolchain_llvm",
            llvm_version = host.llvm_version,
            path = host.path,
        )
    else:
        _llvm_alias_repo(
            name = "llvm_toolchain_llvm",
            minimal_linux_x64 = Label("@llvm_minimal_linux_x64//:BUILD.bazel"),
            minimal_linux_arm64 = Label("@llvm_minimal_linux_arm64//:BUILD.bazel"),
            minimal_macos_arm64 = Label("@llvm_minimal_macos_arm64//:BUILD.bazel"),
        )

_host_llvm = tag_class(
    attrs = {
        "llvm_version": attr.string(default = _LLVM_VERSION),
        "path": attr.string(mandatory = True),
    },
)

envoy_llvm_extension = module_extension(
    implementation = _envoy_llvm_impl,
    tag_classes = {"host": _host_llvm},
)

def _module_dep_from_lock_entry(url):
    if not url.endswith(_SOURCE_JSON_SUFFIX):
        return None

    parts = url.split(_MODULES_SEGMENT)
    if len(parts) != 2:
        fail("Unexpected module registry URL in MODULE.bazel.lock: %s" % url)

    registry = parts[0] + "/"
    module_parts = parts[1][:-len(_SOURCE_JSON_SUFFIX)].split("/")
    if len(module_parts) != 2:
        fail("Unexpected module registry URL in MODULE.bazel.lock: %s" % url)

    return struct(
        module_name = module_parts[0],
        registry = registry,
        version = module_parts[1],
    )

def _envoy_mod_graph_repo_impl(repository_ctx):
    lockfile = json.decode(repository_ctx.read(repository_ctx.attr.lockfile))
    registry_file_hashes = lockfile.get("registryFileHashes", {})
    deps = {}

    for url in registry_file_hashes:
        module_dep = _module_dep_from_lock_entry(url)
        if module_dep == None:
            continue
        if module_dep.module_name in deps:
            fail("MODULE.bazel.lock has multiple source.json entries for module %s" % module_dep.module_name)

        module_url = "%smodules/%s/%s/" % (module_dep.registry, module_dep.module_name, module_dep.version)
        deps[module_dep.module_name] = {
            "module_url": module_url,
            "registry": module_dep.registry,
            "urls": [module_url],
            "version": module_dep.version,
        }

    repository_ctx.file(
        "BUILD.bazel",
        "exports_files([\"deps.json\"], visibility = [\"//visibility:public\"])\n",
    )
    repository_ctx.file("deps.json", json.encode(deps))

_envoy_mod_graph_repo = repository_rule(
    implementation = _envoy_mod_graph_repo_impl,
    attrs = {
        "lockfile": attr.label(allow_single_file = True, mandatory = True),
    },
)

def _envoy_module_graph_impl(module_ctx):
    _envoy_mod_graph_repo(
        name = "envoy_mod_graph",
        lockfile = _LOCKFILE_LABEL,
    )

def _envoy_build_config_impl(module_ctx):
    default_envoy_build_config(name = "envoy_build_config")

envoy_build_config_ext = module_extension(
    implementation = _envoy_build_config_impl,
)

envoy_module_graph_extension = module_extension(
    implementation = _envoy_module_graph_impl,
    doc = """
    Extension that watches MODULE.bazel.lock and materializes resolved module
    dependency metadata as @envoy_mod_graph//:deps.json.
    """,
)

def _envoy_repo_impl(module_ctx):
    """Implementation of the envoy_repo module extension.

    This extension creates the envoy_repo repository which provides version
    information and container metadata for RBE builds.

    Args:
        module_ctx: The module extension context
    """
    envoy_repo()

def _envoy_toolchains_impl(module_ctx):
    """Implementation of the envoy_toolchains module extension.

    This extension registers toolchains needed for Envoy builds in bzlmod mode,
    including the clang_platform alias used in various BUILD files.

    In WORKSPACE mode, this is handled by calling envoy_toolchains() from WORKSPACE.
    In bzlmod mode, we need to use this extension to make the same repositories available.

    Args:
        module_ctx: The module extension context
    """

    # Create the clang_platform repository using arch_alias
    # Note: We can't call envoy_toolchains() directly here because it uses native.register_toolchains
    # which is not allowed in module extensions. Instead, we only create the arch_alias repository.
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": str(Label("//bazel/platforms/rbe:linux_x64")),
            "aarch64": str(Label("//bazel/platforms/rbe:linux_arm64")),
        },
    )

envoy_repo_extension = module_extension(
    implementation = _envoy_repo_impl,
    doc = """
    Extension for the envoy_repo repository.

    This extension creates the @envoy_repo repository which provides:
    - Version information (VERSION, API_VERSION)
    - Container metadata for RBE builds (containers.bzl)
    - LLVM compiler configuration
    - Repository path information

    This is required for RBE toolchain configuration and various build utilities.
    """,
)

envoy_toolchains_extension = module_extension(
    implementation = _envoy_toolchains_impl,
    doc = """
    Extension for Envoy toolchain setup in bzlmod mode.

    This extension creates toolchain-related repositories needed for Envoy builds:
    - clang_platform: Architecture-specific platform aliases for RBE builds

    In WORKSPACE mode, these are created by calling envoy_toolchains() from WORKSPACE.
    In bzlmod mode, this extension provides the same functionality.

    Note: Toolchain registration itself is handled by MODULE.bazel using the LLVM
    toolchain extension. This extension only creates auxiliary repositories.
    """,
)

def _wasm_cargo_impl(module_ctx):
    direct_deps = crate_repositories()
    return module_ctx.extension_metadata(
        root_module_direct_deps = [repo.repo for repo in direct_deps],
        root_module_direct_dev_deps = [],
        reproducible = True,
    )

wasm_cargo = module_extension(implementation = _wasm_cargo_impl)
