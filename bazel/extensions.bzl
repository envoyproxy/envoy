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
    srcs = glob(["lib/**/libclang-cpp.so*", "lib64/**/libclang-cpp.so*"], allow_empty = True),
)

filegroup(
    name = "libllvm",
    srcs = glob(["lib/**/libLLVM.so*", "lib64/**/libLLVM.so*"], allow_empty = True),
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
    return (
        build.replace(
            libclang_glob,
            'glob(["lib/**/libclang.so*", "lib/**/libclang*.dylib", "lib64/**/libclang.so*", "lib64/**/libclang*.dylib"], allow_empty = True)',
        ) + _LLVM_LIBRARY_TARGETS + '\nexports_files(["llvm.bzl"])\n'
    )

def _write_llvm_bzl(repository_ctx, llvm_version, llvm_lib_dir, is_host):
    major = llvm_version.split(".")[0]
    major_minor = ".".join(llvm_version.split(".")[:2])
    repository_ctx.file("llvm.bzl", """
LLVM_VERSION = %r
LLVM_MAJOR = %r
LLVM_MAJOR_MINOR = %r
LLVM_LIB_DIR = %r
LLVM_IS_HOST = %s
""" % (llvm_version, major, major_minor, llvm_lib_dir, is_host))

def _detect_llvm_version(repository_ctx, llvm_root, declared_version, clang_name = "bin/clang"):
    clang = llvm_root.get_child(clang_name)
    result = repository_ctx.execute([str(clang), "--version"])
    if result.return_code != 0:
        fail("Could not run %s --version (exit code %s): %s" % (clang, result.return_code, result.stderr))

    version = ""
    marker = "clang version "
    for line in result.stdout.split("\n"):
        if marker in line:
            version_parts = line[line.find(marker) + len(marker):].strip().split(" ")
            if version_parts:
                version = version_parts[0].split("-")[0]
                components = version.split(".")
                if len(components) != 3:
                    version = ""
                else:
                    for component in components:
                        if not component.isdigit():
                            version = ""
                            break
            break
    if not version:
        fail("Could not parse a full clang version from %s --version output: %s" % (clang, result.stdout))
    if declared_version and declared_version.split(".")[0] != version.split(".")[0]:
        fail(
            "envoy_llvm.host(llvm_version = %r) does not match the installed clang version %s" %
            (declared_version, version),
        )
    return version

def _detect_llvm_lib_dir(llvm_root, major):
    library_name = "libclang-cpp.so.%s" % major
    for candidate in ["lib", "lib64", "lib/x86_64-linux-gnu", "lib/aarch64-linux-gnu"]:
        directory = llvm_root.get_child(candidate)
        if not directory.exists:
            continue
        for library in directory.readdir():
            if library.basename == library_name or library.basename.startswith(library_name + "."):
                return candidate
    fail("Could not find %s under %s in lib, lib64, lib/x86_64-linux-gnu, or lib/aarch64-linux-gnu" % (library_name, llvm_root))

def _host_llvm_repo_impl(repository_ctx):
    llvm_root = repository_ctx.path(repository_ctx.attr.path)
    version = _detect_llvm_version(repository_ctx, llvm_root, repository_ctx.attr.llvm_version)
    lib_dir = _detect_llvm_lib_dir(llvm_root, version.split(".")[0])
    for directory in ["bin", "include", "lib", "lib64"]:
        path = llvm_root.get_child(directory)
        if not path.exists and directory in ["bin", "include"]:
            fail("Host LLVM directory does not exist: %s" % path)
        if path.exists:
            _symlink_directory_contents(repository_ctx, path, directory)
    repository_ctx.file(
        "BUILD.bazel",
        _llvm_repo_build(version),
    )
    _write_llvm_bzl(repository_ctx, version, lib_dir, True)

_host_llvm_repo = repository_rule(
    implementation = _host_llvm_repo_impl,
    local = True,
    attrs = {
        "llvm_version": attr.string(default = ""),
        "path": attr.string(mandatory = True),
    },
)

def _symlink_directory_contents(repository_ctx, source, destination):
    for child in source.readdir():
        repository_ctx.symlink(child, destination + "/" + child.basename)

def _windows_llvm_repo(repository_ctx):
    # Windows builds use the host clang-cl toolchain, so there is no hermetic LLVM to alias.
    # Expose the tools Envoy needs from the host LLVM that clang-cl comes from, located the same
    # way as the rules_cc Windows toolchain does it.
    llvm_root = repository_ctx.path(
        (repository_ctx.getenv("BAZEL_LLVM") or "C:/Program Files/LLVM").replace("\\", "/").rstrip("/"),
    )
    objcopy = llvm_root.get_child("bin/llvm-objcopy.exe")
    if not objcopy.exists:
        fail("Could not find %s. Set BAZEL_LLVM to the host LLVM installation." % objcopy)
    repository_ctx.symlink(objcopy, "bin/llvm-objcopy.exe")
    repository_ctx.file(
        "BUILD.bazel",
        """
package(default_visibility = ["//visibility:public"])

exports_files(["llvm.bzl"])

alias(
    name = "objcopy",
    actual = "bin/llvm-objcopy.exe",
)
""",
    )
    version = _detect_llvm_version(repository_ctx, llvm_root, "", "bin/clang.exe")
    _write_llvm_bzl(repository_ctx, version, "lib", True)

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
    elif os_name.startswith("windows"):
        _windows_llvm_repo(repository_ctx)
        return
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
    _write_llvm_bzl(repository_ctx, _LLVM_VERSION, "lib", False)

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
        "llvm_version": attr.string(default = ""),
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
