#!/usr/bin/env python3
"""Validate dependency metadata against build graph reachability data.

This replaces the old validate.py which used `bazel query` (slow, broken under
bzlmod).  The checks are identical; the data source is the pre-built
``dependency_reachability`` JSON produced by the ``@envoy_toolshed`` aspect.
"""

import json
import os
import pathlib
import re
import unittest

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Package prefixes considered part of the "dataplane core" path set.
DATAPLANE_PACKAGE_PREFIXES = tuple(
    "//source/common/%s/" % p
    for p in [
        "api",
        "buffer",
        "crypto",
        "conn_pool",
        "formatter",
        "http",
        "ssl",
        "tcp",
        "tcp_proxy",
        "network",
    ]
)

CONTROLPLANE_PACKAGE_PREFIX = "//source/common/config/"

EXTENSION_CONSUMER_RE = re.compile(r"^//source/extensions/([^:]+):")

# Internal/tooling deps to skip in all checks (mirrors validate.py's IGNORE_DEPS).
IGNORE_DEPS = frozenset(
    [
        "envoy",
        "envoy_api",
        "envoy_repo",
        "platforms",
        "bazel_tools",
        "local_config_cc",
        "remote_coverage_tools",
        "foreign_cc_platform_utils",
    ]
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _load_json(path):
    return json.loads(pathlib.Path(path).read_text())


def _build_implied_revmap(metadata):
    """Reverse-map untracked transitive deps back to their tracking dep."""
    revmap = {}
    for name, meta in metadata.items():
        for untracked in meta.get("implied_untracked_deps", []):
            revmap[untracked] = name
    return revmap


def _apply_revmap(name, revmap):
    return revmap.get(name, name)


def _deps_by_use_category(metadata, use_category):
    return {k for k, v in metadata.items() if use_category in v.get("use_category", [])}


def _is_local_consumer(consumer):
    """True if the consumer is an in-repo target (not an external-repo target)."""
    return not consumer["target"].startswith("@@")


def _extension_package(target):
    """Return ``//source/extensions/<pkg>`` or None."""
    m = EXTENSION_CONSUMER_RE.match(target)
    return ("//source/extensions/" + m.group(1)) if m else None


def _core_consumer(target):
    """True if the consumer makes a dep non-marginal for the extension check.

    A dep is considered "core" (not extension-marginal) if it has any consumer
    that is:
    - a local in-repo target outside of ``//source/extensions/``, OR
    - a target in an external repository (@@...), which means the dep is pulled
      in transitively by another external dep that is itself a core dep.
    """
    if target.startswith("@@"):
        return True
    return target.startswith("//source/") and not target.startswith("//source/extensions/")


# ---------------------------------------------------------------------------
# Validation logic (mirrors validate.py check-by-check)
# ---------------------------------------------------------------------------


def validate_test_only_deps(deps, metadata):
    """No test_only-marked dep may be reachable via a non-testonly (production) path."""
    test_only = _deps_by_use_category(metadata, "test_only")

    bad = set()
    for dep_data in deps.values():
        name = dep_data["name"]
        if dep_data["production"] and name in test_only:
            bad.add(name)

    if bad:
        raise AssertionError(
            "//source depends on test-only dependencies: %s" % sorted(bad)
        )


def validate_data_plane_core_deps(deps, metadata, revmap):
    """Deps reached by dataplane paths must carry dataplane_core or api category."""
    expected = _deps_by_use_category(metadata, "dataplane_core") | _deps_by_use_category(
        metadata, "api"
    )

    observed = set()
    for dep_data in deps.values():
        name = _apply_revmap(dep_data["name"], revmap)
        if name in IGNORE_DEPS:
            continue
        for consumer in dep_data["consumers"]:
            if any(
                consumer["target"].startswith(pfx) for pfx in DATAPLANE_PACKAGE_PREFIXES
            ):
                observed.add(name)
                break

    # boringssl_fips is the same library as boringssl; ignore it.
    observed.discard("boringssl_fips")

    bad = observed - expected
    if bad:
        raise AssertionError(
            "Observed dataplane core deps %s not covered by use_category: %s are missing"
            % (sorted(observed), sorted(bad))
        )


def validate_control_plane_deps(deps, metadata, revmap):
    """Deps reached by the controlplane path must carry controlplane or api category."""
    expected = _deps_by_use_category(metadata, "controlplane") | _deps_by_use_category(
        metadata, "api"
    )

    observed = set()
    for dep_data in deps.values():
        name = _apply_revmap(dep_data["name"], revmap)
        if name in IGNORE_DEPS:
            continue
        for consumer in dep_data["consumers"]:
            if consumer["target"].startswith(CONTROLPLANE_PACKAGE_PREFIX):
                observed.add(name)
                break

    observed.discard("boringssl_fips")

    bad = observed - expected
    if bad:
        raise AssertionError(
            "Observed controlplane core deps %s not covered by use_category: %s are missing"
            % (sorted(observed), sorted(bad))
        )


def validate_extension_deps(deps, metadata, revmap, extensions_build_config):
    """Per-extension marginal deps must carry an ext/observability/other/api category.

    ``extensions_build_config`` is a dict mapping extension-name → target label.
    Marginal deps for extension X are deps whose *only* in-repo consumers are
    targets under ``//source/extensions/X/...`` (no core consumer exists).
    """
    # Build package-path → [extension-name] mapping from the config.
    pkg_to_ext: dict = {}
    for ext_name, target in extensions_build_config.items():
        m = re.match(r"(//source/extensions/[^:]+):", target)
        if m:
            pkg = m.group(1)
            pkg_to_ext.setdefault(pkg, []).append(ext_name)

    # Determine which dep names are "core" (have at least one core consumer).
    # _core_consumer includes @@-prefixed (external-repo) consumers because they
    # indicate the dep is pulled in transitively by another external dep, not
    # solely by an extension.
    core_dep_names = set()
    for dep_data in deps.values():
        name = _apply_revmap(dep_data["name"], revmap)
        for consumer in dep_data["consumers"]:
            if _core_consumer(consumer["target"]):
                core_dep_names.add(name)
                break

    # For each extension package, collect the marginal deps it introduces.
    ext_pkg_deps: dict = {}
    for dep_data in deps.values():
        name = _apply_revmap(dep_data["name"], revmap)
        if name in core_dep_names:
            continue
        for consumer in dep_data["consumers"]:
            pkg = _extension_package(consumer["target"])
            if pkg:
                ext_pkg_deps.setdefault(pkg, set()).add(name)

    errors = []
    for pkg, ext_names in pkg_to_ext.items():
        for dep_name in ext_pkg_deps.get(pkg, set()):
            meta = metadata.get(dep_name)
            if not meta:
                continue
            use_category = meta.get("use_category", [])
            valid_category = any(
                c in use_category
                for c in ["dataplane_ext", "observability_ext", "other", "api"]
            )
            if not valid_category:
                errors.append(
                    "Extension %s (package %s) depends on %s with use_category %s "
                    "not including dataplane_ext/observability_ext/api/other"
                    % (ext_names[0], pkg, dep_name, use_category)
                )
            if "extensions" in meta:
                for ext_name in ext_names:
                    if ext_name not in meta["extensions"]:
                        errors.append(
                            "Extension %s depends on %s but %s does not list %s "
                            "in its extensions allowlist"
                            % (ext_name, dep_name, dep_name, ext_name)
                        )

    if errors:
        raise AssertionError(
            "Extension dependency validation errors:\n" + "\n".join(errors)
        )


# ---------------------------------------------------------------------------
# Test class
# ---------------------------------------------------------------------------


class ValidateReachabilityTest(unittest.TestCase):
    """Reads pre-built reachability JSON and validates dependency metadata."""

    @classmethod
    def setUpClass(cls):
        # Paths are passed via environment variables set by the py_test wrapper.
        reach_path = os.environ["REACHABILITY_JSON"]
        meta_path = os.environ["REPOSITORY_LOCATIONS_JSON"]
        ext_cfg_path = os.environ["EXTENSIONS_BUILD_CONFIG_JSON"]

        raw = _load_json(reach_path)
        cls.deps = raw["dependencies"]
        cls.metadata = _load_json(meta_path)
        cls.extensions_build_config = _load_json(ext_cfg_path)
        cls.revmap = _build_implied_revmap(cls.metadata)

    def test_test_only_deps(self):
        validate_test_only_deps(self.deps, self.metadata)

    def test_data_plane_core_deps(self):
        validate_data_plane_core_deps(self.deps, self.metadata, self.revmap)

    def test_control_plane_deps(self):
        validate_control_plane_deps(self.deps, self.metadata, self.revmap)

    def test_extension_deps(self):
        validate_extension_deps(
            self.deps, self.metadata, self.revmap, self.extensions_build_config
        )


if __name__ == "__main__":
    unittest.main()
