#!/usr/bin/env python3
"""Validate the relationship between Envoy dependencies and core/extensions.

This script verifies that bazel query of the build graph is consistent with
the use_category metadata in bazel/repository_locations.bzl.
"""

import asyncio
import json
import pathlib
import re
import sys

from aio.api import bazel

import envoy_repo

BAZEL_QUERY_EXTERNAL_DEP_RE = re.compile(r'@(\w+)//')
EXTENSION_LABEL_RE = re.compile(r'(//source/extensions/.*):')

# We can safely ignore these as they are from Bazel or internal repository structure.
IGNORE_DEPS = set([
    'envoy',
    'envoy_api',
    'envoy_api',
    'platforms',
    'bazel_tools',
    'local_config_cc',
    'remote_coverage_tools',
    'foreign_cc_platform_utils',
])


# Should a dependency be ignored if it's only used in test? Any changes to this
# allowlist method should be accompanied by an update to the explanation in the
# "Test only" section of
# docs/root/intro/arch_overview/security/external_deps.rst.
def test_only_ignore(dep):
    # Rust
    if dep.startswith('raze__'):
        return True
    # Java
    if dep.startswith('remotejdk'):
        return True
    # Python (pip3)
    if '_pip3' in dep:
        return True
    return False


query = bazel.BazelEnv(envoy_repo.PATH).query


class DependencyError(Exception):
    """Error in dependency relationships."""
    pass


class DependencyInfo:
    """Models dependency info in bazel/repositories.bzl."""

    def __init__(self, repository_locations):
        self.repository_locations = repository_locations

    def deps_by_use_category(self, use_category):
        """Find the set of external dependencies in a given use_category.

        Args:
          use_category: string providing use_category.

        Returns:
          Set of dependency identifiers that match use_category.
        """
        return set(
            name for name, metadata in self.repository_locations.items()
            if use_category in metadata['use_category'])

    def get_metadata(self, dependency):
        """Obtain repository metadata for a dependency.

        Args:
          dependency: string providing dependency identifier.

        Returns:
          A dictionary with the repository metadata as defined in
            bazel/repository_locations.bzl.
        """
        return self.repository_locations.get(dependency)


class BuildGraph:
    """Models the Bazel build graph."""

    def __init__(self, extensions_build_config, ignore_deps=IGNORE_DEPS, repository_locations=None):
        self.extensions_build_config = extensions_build_config
        self._ignore_deps = ignore_deps
        # Reverse map from untracked dependencies implied by other deps back to the dep.
        self._implied_untracked_deps_revmap = {}
        for dep, metadata in repository_locations.items():
            implied_untracked_deps = metadata.get('implied_untracked_deps', [])
            for untracked_dep in implied_untracked_deps:
                self._implied_untracked_deps_revmap[untracked_dep] = dep

    async def query_external_deps(self, *targets, exclude=None):
        """Query the build graph for transitive external dependencies.

        Args:
          targets: Bazel targets.

        Returns:
          A set of dependency identifiers that are reachable from targets.
        """
        # Get the smallest sets possible to fulfill the query!
        deps_query = self._filtered_deps_query(targets)
        exclude_deps = set()
        if exclude:
            exclude_query = self._filtered_deps_query(exclude)
            deps_query = f'{deps_query} - {exclude_query}'
        try:
            deps = await self._deps_query(deps_query)
            if deps and exclude:
                # although the deps set is pre-filtered to exclude
                # the excluded deps, we still need to fetch the exclude set
                # again and remove any further matches, due to rev dep mangling.
                # The exclude set could be pre-filtered further (ie only members
                # of the revmap.values) at the cost of some additional complexity.
                exclude_deps = await self._deps_query(exclude_query)
        except bazel.BazelQueryError as e:
            print(f'Bazel query failed with error {e}')
            raise e
        return deps - exclude_deps

    async def _deps_query(self, query_string):
        return self._mangle_deps_set(await query(query_string))

    def _filtered_deps_query(self, targets):
        return f'filter("^@.*//", deps(set({" ".join(targets)})))'

    def _mangle_deps_set(self, deps):
        ext_deps = set()
        implied_untracked_deps = set()
        for d in deps:
            matched = BAZEL_QUERY_EXTERNAL_DEP_RE.match(d)
            if not matched:
                continue
            ext_dep = matched.group(1)
            if ext_dep in self._ignore_deps:
                continue
            # If the dependency is untracked, add the source dependency that loaded
            # it transitively.
            if ext_dep in self._implied_untracked_deps_revmap:
                ext_dep = self._implied_untracked_deps_revmap[ext_dep]
            ext_deps.add(ext_dep)
        return set(ext_deps)

    def list_extensions(self):
        """List all extensions.

        Returns:
          Dictionary items from source/extensions/extensions_build_config.bzl.
        """
        return self.extensions_build_config.items()


class Validator(object):
    """Collection of validation methods."""
    _core_rule_label = '//source/exe:envoy_main_common_with_core_extensions_lib'

    def __init__(self, dep_info, build_graph):
        self._dep_info = dep_info
        self._build_graph = build_graph

    async def validate_build_graph_structure(self):
        """Validate basic assumptions about dependency relationship in the build graph.

        Raises:
          DependencyError: on a dependency validation error.
        """
        print('Validating build dependency structure...')
        queried_core_ext_deps = await self._build_graph.query_external_deps(
            self._core_rule_label, '//source/extensions/...', exclude=['//source/...'])
        queried_all_deps = await self._build_graph.query_external_deps(
            '//source/...', exclude=[self._core_rule_label, '//source/extensions/...'])
        if queried_all_deps or queried_core_ext_deps:
            raise DependencyError(
                'Invalid build graph structure. deps(//source/...) != '
                'deps(//source/exe:envoy_main_common_with_core_extensions_lib) '
                'union deps(//source/extensions/...)')

    async def validate_test_only_deps(self):
        """Validate that test-only dependencies aren't included in //source/...

        Raises:
          DependencyError: on a dependency validation error.
        """
        # Validate that //source doesn't depend on test_only
        queried_source_deps = await self._build_graph.query_external_deps('//source/...')
        expected_test_only_deps = self._dep_info.deps_by_use_category('test_only')
        bad_test_only_deps = expected_test_only_deps.intersection(queried_source_deps)
        if len(bad_test_only_deps) > 0:
            raise DependencyError(
                f'//source depends on test-only dependencies: {bad_test_only_deps}')
        # Validate that //test deps additional to those of //source are captured in
        # test_only.
        marginal_test_deps = await self._build_graph.query_external_deps(
            '//test/...', exclude=['//source/...'])
        bad_test_deps = marginal_test_deps.difference(expected_test_only_deps)
        unknown_bad_test_deps = [dep for dep in bad_test_deps if not test_only_ignore(dep)]
        print(f'Validating {len(expected_test_only_deps)} test-only dependencies...')
        if len(unknown_bad_test_deps) > 0:
            raise DependencyError(
                f'Missing deps in test_only "use_category": {unknown_bad_test_deps}')

    async def validate_data_plane_core_deps(self):
        """Validate dataplane_core dependencies.

        Check that we at least tag as dataplane_core dependencies that match some
        well-known targets for the data-plane.

        Raises:
          DependencyError: on a dependency validation error.
        """
        # Necessary but not sufficient for dataplane. With some refactoring we could
        # probably have more precise tagging of dataplane/controlplane/other deps in
        # these paths.
        queried_dataplane_core_min_deps = await self._build_graph.query_external_deps(
            '//source/common/api/...', '//source/common/buffer/...', '//source/common/crypto/...',
            '//source/common/conn_pool/...', '//source/common/formatter/...',
            '//source/common/http/...', '//source/common/ssl/...', '//source/common/tcp/...',
            '//source/common/tcp_proxy/...', '//source/common/network/...')
        # It's hard to disentangle API and dataplane today.
        expected_dataplane_core_deps = self._dep_info.deps_by_use_category('dataplane_core').union(
            self._dep_info.deps_by_use_category('api'))
        bad_dataplane_core_deps = queried_dataplane_core_min_deps.difference(
            expected_dataplane_core_deps)
        print(f'Validating {len(expected_dataplane_core_deps)} data-plane dependencies...')
        if len(bad_dataplane_core_deps) > 0:
            raise DependencyError(
                f'Observed dataplane core deps {queried_dataplane_core_min_deps} is not covered by '
                f'"use_category" implied core deps {expected_dataplane_core_deps}: {bad_dataplane_core_deps} '
                'are missing')

    async def validate_control_plane_deps(self):
        """Validate controlplane dependencies.

        Check that we at least tag as controlplane dependencies that match some
        well-known targets for the control-plane.

        Raises:
          DependencyError: on a dependency validation error.
        """
        # Necessary but not sufficient for controlplane. With some refactoring we could
        # probably have more precise tagging of dataplane/controlplane/other deps in
        # these paths.
        queried_controlplane_core_min_deps = await self._build_graph.query_external_deps(
            '//source/common/config/...')
        # Controlplane will always depend on API.
        expected_controlplane_core_deps = self._dep_info.deps_by_use_category('controlplane').union(
            self._dep_info.deps_by_use_category('api'))
        bad_controlplane_core_deps = queried_controlplane_core_min_deps.difference(
            expected_controlplane_core_deps)
        print(f'Validating {len(expected_controlplane_core_deps)} control-plane dependencies...')
        if len(bad_controlplane_core_deps) > 0:
            raise DependencyError(
                f'Observed controlplane core deps {queried_controlplane_core_min_deps} is not covered '
                f'by "use_category" implied core deps {expected_controlplane_core_deps}: '
                f'{bad_controlplane_core_deps} are missing')

    async def validate_extension_deps(self, name, target):
        """Validate that extensions are correctly declared for dataplane_ext and observability_ext.

        Args:
          name: extension name.
          target: extension Bazel target.

        Raises:
          DependencyError: on a dependency validation error.
        """
        marginal_deps = await self._build_graph.query_external_deps(
            target, exclude=['//source/exe:envoy_main_common_with_core_extensions_lib'])
        expected_deps = []
        print(f'Validating ({len(marginal_deps)}) {name} extension dependencies...')
        for d in marginal_deps:
            metadata = self._dep_info.get_metadata(d)
            if metadata:
                use_category = metadata['use_category']
                valid_use_category = any(
                    c in use_category
                    for c in ['dataplane_ext', 'observability_ext', 'other', 'api'])
                if not valid_use_category:
                    raise DependencyError(
                        f'Extensions {name} depends on {d} with "use_category" not including '
                        '["dataplane_ext", "observability_ext", "api", "other"]')
                if 'extensions' in metadata:
                    allowed_extensions = metadata['extensions']
                    if name not in allowed_extensions:
                        raise DependencyError(
                            f'Extension {name} depends on {d} but {d} does not list {name} in its allowlist'
                        )

    async def validate_all(self):
        """Collection of all validations.

        Raises:
          DependencyError: on a dependency validation error.
        """
        await self.validate_build_graph_structure()
        await self.validate_test_only_deps()
        await self.validate_data_plane_core_deps()
        await self.validate_control_plane_deps()
        # Validate the marginal dependencies introduced for each extension.
        for name, target in sorted(build_graph.list_extensions()):
            target_all = EXTENSION_LABEL_RE.match(target).group(1) + '/...'
            await self.validate_extension_deps(name, target_all)


if __name__ == '__main__':
    repository_locations = json.loads(pathlib.Path(sys.argv[1]).read_text())
    extensions_build_config = json.loads(pathlib.Path(sys.argv[2]).read_text())
    dep_info = DependencyInfo(repository_locations=repository_locations)
    build_graph = BuildGraph(extensions_build_config, repository_locations=repository_locations)
    validator = Validator(dep_info, build_graph)
    try:
        asyncio.run(validator.validate_all())
    except DependencyError as e:
        print(
            'Dependency validation failed, please check metadata in bazel/repository_locations.bzl')
        print(e)
        sys.exit(1)
