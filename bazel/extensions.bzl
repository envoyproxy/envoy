"""Module extensions for Envoy build configuration, dependency metadata, and repositories."""

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

# TODO(phlax): Remove once envoy-examples (and any other registry modules) use
# @envoy_toolshed//repository:extensions.bzl%arch_alias_extension directly.
def _envoy_toolchains_impl(module_ctx):
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
