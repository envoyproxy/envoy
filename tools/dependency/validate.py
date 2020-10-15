#!/usr/bin/env python3
"""Validate the relationship between Envoy dependencies and core/extensions.

This script verifies that bazel query of the build graph is consistent with
the use_category metadata in bazel/repository_locations.bzl.
"""

import re
import subprocess
import sys

from importlib.machinery import SourceFileLoader
from importlib.util import spec_from_loader, module_from_spec

# bazel/repository_locations.bzl must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
_repository_locations_spec = spec_from_loader(
    'repository_locations',
    SourceFileLoader('repository_locations', 'bazel/repository_locations.bzl'))
repository_locations = module_from_spec(_repository_locations_spec)
_repository_locations_spec.loader.exec_module(repository_locations)

# source/extensions/extensions_build_config.bzl must have a .bzl suffix for Starlark
# import, so we are forced to do this workaround.
_extensions_build_config_spec = spec_from_loader(
    'extensions_build_config',
    SourceFileLoader('extensions_build_config', 'source/extensions/extensions_build_config.bzl'))
extensions_build_config = module_from_spec(_extensions_build_config_spec)
_extensions_build_config_spec.loader.exec_module(extensions_build_config)

BAZEL_QUERY_EXTERNAL_DEP_RE = re.compile('@(\w+)//')
EXTENSION_LABEL_RE = re.compile('(//source/extensions/.*):')

# TODO(htuch): Add API dependencies to metadata, shrink this set.
UNKNOWN_DEPS = [
    'org_golang_x_tools', 'com_github_cncf_udpa', 'org_golang_google_protobuf',
    'io_bazel_rules_nogo', 'com_envoyproxy_protoc_gen_validate', 'opencensus_proto',
    'io_bazel_rules_go', 'foreign_cc_platform_utils', 'com_github_golang_protobuf',
    'com_google_googleapis'
]
IGNORE_DEPS = set([
    'envoy', 'envoy_api', 'envoy_api_canonical', 'platforms', 'bazel_tools', 'local_config_cc',
    'remote_coverage_tools'
] + UNKNOWN_DEPS)


# Should a dependency be ignored if it's only used in test? Any changes to this
# allowlist method should be accompanied by an update to the explanation in the
# "Test only" section of
# docs/root/intro/arch_overview/security/external_deps.rst.
def TestOnlyIgnore(dep):
  # Rust
  if dep.startswith('raze__'):
    return True
  # Java
  if dep.startswith('remotejdk'):
    return True
  # Python (pip3)
  if '_pip3_' in dep:
    return True
  return False


class DependencyError(Exception):
  """Error in dependency relationships."""
  pass


class DependencyInfo(object):
  """Models dependency info in bazel/repositories.bzl."""

  def DepsByUseCategory(self, use_category):
    """Find the set of external dependencies in a given use_category.

    Args:
      use_category: string providing use_category.

    Returns:
      Set of dependency identifiers that match use_category.
    """
    return set(name for name, metadata in repository_locations.REPOSITORY_LOCATIONS_SPEC.items()
               if use_category in metadata['use_category'])

  def GetMetadata(self, dependency):
    """Obtain repository metadata for a dependency.

    Args:
      dependency: string providing dependency identifier.

    Returns:
      A dictionary with the repository metadata as defined in
      bazel/repository_locations.bzl.
    """
    return repository_locations.REPOSITORY_LOCATIONS_SPEC.get(dependency)


class BuildGraph(object):
  """Models the Bazel build graph."""

  def QueryExternalDeps(self, *targets):
    """Query the build graph for transitive external dependencies.

    Args:
      targets: Bazel targets.

    Returns:
      A set of dependency identifiers that are reachable from targets.
    """
    deps_query = ' union '.join(f'deps({l})' for l in targets)
    deps = subprocess.check_output(['bazel', 'query', deps_query],
                                   stderr=subprocess.PIPE).decode().splitlines()
    ext_deps = set()
    for d in deps:
      match = BAZEL_QUERY_EXTERNAL_DEP_RE.match(d)
      if match:
        ext_dep = match.group(1)
        if ext_dep not in IGNORE_DEPS:
          ext_deps.add(ext_dep)
    return set(ext_deps)

  def ListExtensions(self):
    """List all extensions.

    Returns:
      Dictionary items from source/extensions/extensions_build_config.bzl.
    """
    return extensions_build_config.EXTENSIONS.items()


class Validator(object):
  """Collection of validation methods."""

  def __init__(self, dep_info, build_graph):
    self._dep_info = dep_info
    self._build_graph = build_graph
    self._queried_core_deps = build_graph.QueryExternalDeps(
        '//source/exe:envoy_main_common_with_core_extensions_lib')

  def ValidateBuildGraphStructure(self):
    """Validate basic assumptions about dependency relationship in the build graph.

    Raises:
      DependencyError: on a dependency validation error.
    """
    print('Validating build dependency structure...')
    queried_core_ext_deps = self._build_graph.QueryExternalDeps(
        '//source/exe:envoy_main_common_with_core_extensions_lib', '//source/extensions/...')
    queried_all_deps = self._build_graph.QueryExternalDeps('//source/...')
    if queried_all_deps != queried_core_ext_deps:
      raise DependencyError('Invalid build graph structure. deps(//source/...) != '
                            'deps(//source/exe:envoy_main_common_with_core_extensions_lib) '
                            'union deps(//source/extensions/...)')

  def ValidateTestOnlyDeps(self):
    """Validate that test-only dependencies aren't included in //source/...

    Raises:
      DependencyError: on a dependency validation error.
    """
    print('Validating test-only dependencies...')
    # Validate that //source doesn't depend on test_only
    queried_source_deps = self._build_graph.QueryExternalDeps('//source/...')
    expected_test_only_deps = self._dep_info.DepsByUseCategory('test_only')
    bad_test_only_deps = expected_test_only_deps.intersection(queried_source_deps)
    if len(bad_test_only_deps) > 0:
      raise DependencyError(f'//source depends on test-only dependencies: {bad_test_only_deps}')
    # Validate that //test deps additional to those of //source are captured in
    # test_only.
    test_only_deps = self._build_graph.QueryExternalDeps('//test/...')
    source_deps = self._build_graph.QueryExternalDeps('//source/...')
    marginal_test_deps = test_only_deps.difference(source_deps)
    bad_test_deps = marginal_test_deps.difference(expected_test_only_deps)
    unknown_bad_test_deps = [dep for dep in bad_test_deps if not TestOnlyIgnore(dep)]
    if len(unknown_bad_test_deps) > 0:
      raise DependencyError(f'Missing deps in test_only "use_category": {unknown_bad_test_deps}')

  def ValidateDataPlaneCoreDeps(self):
    """Validate dataplane_core dependencies.

    Check that we at least tag as dataplane_core dependencies that match some
    well-known targets for the data-plane.

    Raises:
      DependencyError: on a dependency validation error.
    """
    print('Validating data-plane dependencies...')
    # Necessary but not sufficient for dataplane. With some refactoring we could
    # probably have more precise tagging of dataplane/controlplane/other deps in
    # these paths.
    queried_dataplane_core_min_deps = self._build_graph.QueryExternalDeps(
        '//source/common/api/...', '//source/common/buffer/...', '//source/common/chromium_url/...',
        '//source/common/crypto/...', '//source/common/conn_pool/...',
        '//source/common/formatter/...', '//source/common/http/...', '//source/common/ssl/...',
        '//source/common/tcp/...', '//source/common/tcp_proxy/...', '//source/common/network/...')
    expected_dataplane_core_deps = self._dep_info.DepsByUseCategory('dataplane_core')
    bad_dataplane_core_deps = queried_dataplane_core_min_deps.difference(
        expected_dataplane_core_deps)
    if len(bad_dataplane_core_deps) > 0:
      raise DependencyError(
          f'Observed dataplane core deps {queried_dataplane_core_min_deps} is not covered by '
          '"use_category" implied core deps {expected_dataplane_core_deps}: {bad_dataplane_core_deps} '
          'are missing')

  def ValidateControlPlaneDeps(self):
    """Validate controlplane dependencies.

    Check that we at least tag as controlplane dependencies that match some
    well-known targets for
    the control-plane.

    Raises:
      DependencyError: on a dependency validation error.
    """
    print('Validating control-plane dependencies...')
    # Necessary but not sufficient for controlplane. With some refactoring we could
    # probably have more precise tagging of dataplane/controlplane/other deps in
    # these paths.
    queried_controlplane_core_min_deps = self._build_graph.QueryExternalDeps(
        '//source/common/config/...')
    expected_controlplane_core_deps = self._dep_info.DepsByUseCategory('controlplane')
    bad_controlplane_core_deps = queried_controlplane_core_min_deps.difference(
        expected_controlplane_core_deps)
    if len(bad_controlplane_core_deps) > 0:
      raise DependencyError(
          f'Observed controlplane core deps {queried_controlplane_core_min_deps} is not covered '
          'by "use_category" implied core deps {expected_controlplane_core_deps}: '
          '{bad_controlplane_core_deps} are missing')

  def ValidateExtensionDeps(self, name, target):
    """Validate that extensions are correctly declared for dataplane_ext and observability_ext.

    Args:
      name: extension name.
      target: extension Bazel target.

    Raises:
      DependencyError: on a dependency validation error.
    """
    print(f'Validating extension {name} dependencies...')
    queried_deps = self._build_graph.QueryExternalDeps(target)
    marginal_deps = queried_deps.difference(self._queried_core_deps)
    expected_deps = []
    for d in marginal_deps:
      # TODO(htuch): Ensure that queried deps are fully contained in
      # repository_locations, i.e. that we're tracking with metadata all actual
      # dependencies. Today, we are missing API and pip3 deps based on manual
      # inspection.
      metadata = self._dep_info.GetMetadata(d)
      if metadata:
        use_category = metadata['use_category']
        valid_use_category = any(
            c in use_category for c in ['dataplane_ext', 'observability_ext', 'other'])
        if not valid_use_category:
          raise DependencyError(
              f'Extensions {name} depends on {d} with "use_category" not including '
              '["dataplane_ext", "observability_ext", "other"]')
        if 'extensions' in metadata:
          allowed_extensions = metadata['extensions']
          if name not in allowed_extensions:
            raise DependencyError(
                f'Extension {name} depends on {d} but {d} does not list {name} in its allowlist')

  def ValidateAll(self):
    """Collection of all validations.

    Raises:
      DependencyError: on a dependency validation error.
    """
    self.ValidateBuildGraphStructure()
    self.ValidateTestOnlyDeps()
    self.ValidateDataPlaneCoreDeps()
    self.ValidateControlPlaneDeps()
    # Validate the marginal dependencies introduced for each extension.
    for name, target in sorted(build_graph.ListExtensions()):
      target_all = EXTENSION_LABEL_RE.match(target).group(1) + '/...'
      self.ValidateExtensionDeps(name, target_all)


if __name__ == '__main__':
  dep_info = DependencyInfo()
  build_graph = BuildGraph()
  validator = Validator(dep_info, build_graph)
  try:
    validator.ValidateAll()
  except DependencyError as e:
    print('Dependency validation failed, please check metadata in bazel/repository_locations.bzl')
    print(e)
    sys.exit(1)
