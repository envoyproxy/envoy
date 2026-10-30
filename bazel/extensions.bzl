"""Module extensions for Envoy build configuration, dependency metadata, and repositories."""

load("@envoy_toolshed//compile:llvm_minimal.bzl", "llvm_toolchain_alias", "setup_llvm_minimal")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load("//bazel/external/cargo/remote:crates.bzl", "crate_repositories")
load(":envoy_build_config.bzl", "default_envoy_build_config")
load(":repo.bzl", "envoy_repo")

_LOCKFILE_LABEL = Label("//:MODULE.bazel.lock")
_MODULES_SEGMENT = "/modules/"
_SOURCE_JSON_SUFFIX = "/source.json"

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

def _wasm_cargo_impl(module_ctx):
    direct_deps = crate_repositories()
    return module_ctx.extension_metadata(
        root_module_direct_deps = [repo.repo for repo in direct_deps],
        root_module_direct_dev_deps = [],
        reproducible = True,
    )

wasm_cargo = module_extension(implementation = _wasm_cargo_impl)

_LLVM_MIGRATION = """
`envoy_llvm_extension` (@envoy//bazel:extensions.bzl) is deprecated and will be removed.
Migrate your MODULE.bazel to the toolshed extension:

    llvm_toolchain_alias_ext = use_extension(
        "@envoy_toolshed//compile:extensions.bzl",
        "llvm_toolchain_alias_extension",
    )
    # Optional, root module only, to use a host LLVM install:
    # llvm_toolchain_alias_ext.host(path = "/usr/lib/llvm-22")
    use_repo(llvm_toolchain_alias_ext, "llvm_toolchain_llvm")

See bazel/README.md and bazel/tests/codeql for a complete example.
"""

# TODO(phlax): Remove once downstream modules have migrated.
def _envoy_llvm_impl(module_ctx):
    # buildifier: disable=print
    print("DEPRECATED: " + _LLVM_MIGRATION)
    for module in module_ctx.modules:
        if module.tags.host:
            # A host tag here can no longer redirect Envoy's own @llvm_toolchain_llvm
            # (that now comes from the toolshed extension), so fail rather than
            # silently produce an unused repo.
            fail(
                "envoy_llvm_extension.host() is no longer supported (set by module %r)." % module.name +
                _LLVM_MIGRATION,
            )

    # Deprecated no-tag path: mirror toolshed's llvm_toolchain_alias_extension so an
    # existing `use_repo(envoy_llvm_ext, "llvm_toolchain_llvm")` keeps resolving.
    setup_llvm_minimal()
    llvm_toolchain_alias(
        name = "llvm_toolchain_llvm",
        minimal_linux_x64 = "@llvm_minimal_linux_x64//:BUILD.bazel",
        minimal_linux_arm64 = "@llvm_minimal_linux_arm64//:BUILD.bazel",
        minimal_macos_arm64 = "@llvm_minimal_macos_arm64//:BUILD.bazel",
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
    doc = "Deprecated: use @envoy_toolshed//compile:extensions.bzl%llvm_toolchain_alias_extension.",
)

# TODO(phlax): Remove once envoy-examples (and any other registry modules) use
# @envoy_toolshed//repository:extensions.bzl%arch_alias_extension directly.
def _envoy_toolchains_impl(module_ctx):
    # buildifier: disable=print
    print(
        "DEPRECATED: `envoy_toolchains_extension` is deprecated; use " +
        "@envoy_toolshed//repository:extensions.bzl%arch_alias_extension with " +
        "`.alias(name = \"clang_platform\", amd64 = \"@envoy//bazel/platforms/rbe:linux_x64\", " +
        "aarch64 = \"@envoy//bazel/platforms/rbe:linux_arm64\")` and " +
        "`use_repo(arch_alias_ext, \"clang_platform\")`.",
    )
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": str(Label("//bazel/platforms/rbe:linux_x64")),
            "aarch64": str(Label("//bazel/platforms/rbe:linux_arm64")),
        },
    )

envoy_toolchains_extension = module_extension(
    implementation = _envoy_toolchains_impl,
    doc = "Deprecated: use @envoy_toolshed arch_alias_extension instead.",
)
