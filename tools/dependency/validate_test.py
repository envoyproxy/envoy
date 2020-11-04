#!/usr/bin/env python3
"""Tests for validate.py"""

import unittest

import validate


class FakeDependencyInfo(object):
  """validate.DependencyInfo fake."""

  def __init__(self, deps):
    self._deps = deps

  def DepsByUseCategory(self, use_category):
    return set(n for n, m in self._deps.items() if use_category in m['use_category'])

  def GetMetadata(self, dependency):
    return self._deps.get(dependency)


class FakeBuildGraph(object):
  """validate.BuildGraph fake."""

  def __init__(self, reachable_deps, extensions):
    self._reachable_deps = reachable_deps
    self._extensions = extensions

  def QueryExternalDeps(self, *targets):
    return set(sum((self._reachable_deps.get(t, []) for t in targets), []))

  def ListExtensions(self):
    return self._extensions


def FakeDep(use_category, extensions=[]):
  return {'use_category': use_category, 'extensions': extensions}


class ValidateTest(unittest.TestCase):

  def BuildValidator(self, deps, reachable_deps, extensions=[]):
    return validate.Validator(FakeDependencyInfo(deps), FakeBuildGraph(reachable_deps, extensions))

  def test_valid_build_graph_structure(self):
    validator = self.BuildValidator({}, {
        '//source/exe:envoy_main_common_with_core_extensions_lib': ['a'],
        '//source/extensions/...': ['b'],
        '//source/...': ['a', 'b']
    })
    validator.ValidateBuildGraphStructure()

  def test_invalid_build_graph_structure(self):
    validator = self.BuildValidator({}, {
        '//source/exe:envoy_main_common_with_core_extensions_lib': ['a'],
        '//source/extensions/...': ['b'],
        '//source/...': ['a', 'b', 'c']
    })
    self.assertRaises(validate.DependencyError, lambda: validator.ValidateBuildGraphStructure())

  def test_valid_test_only_deps(self):
    validator = self.BuildValidator({'a': FakeDep('dataplane_core')}, {'//source/...': ['a']})
    validator.ValidateTestOnlyDeps()
    validator = self.BuildValidator({'a': FakeDep('test_only')}, {'//test/...': ['a', 'b__pip3_']})
    validator.ValidateTestOnlyDeps()

  def test_invalid_test_only_deps(self):
    validator = self.BuildValidator({'a': FakeDep('test_only')}, {'//source/...': ['a']})
    self.assertRaises(validate.DependencyError, lambda: validator.ValidateTestOnlyDeps())
    validator = self.BuildValidator({'a': FakeDep('test_only')}, {'//test/...': ['b']})
    self.assertRaises(validate.DependencyError, lambda: validator.ValidateTestOnlyDeps())

  def test_valid_dataplane_core_deps(self):
    validator = self.BuildValidator({'a': FakeDep('dataplane_core')},
                                    {'//source/common/http/...': ['a']})
    validator.ValidateDataPlaneCoreDeps()

  def test_invalid_dataplane_core_deps(self):
    validator = self.BuildValidator({'a': FakeDep('controlplane')},
                                    {'//source/common/http/...': ['a']})
    self.assertRaises(validate.DependencyError, lambda: validator.ValidateDataPlaneCoreDeps())

  def test_valid_controlplane_deps(self):
    validator = self.BuildValidator({'a': FakeDep('controlplane')},
                                    {'//source/common/config/...': ['a']})
    validator.ValidateControlPlaneDeps()

  def test_invalid_controlplane_deps(self):
    validator = self.BuildValidator({'a': FakeDep('other')}, {'//source/common/config/...': ['a']})
    self.assertRaises(validate.DependencyError, lambda: validator.ValidateControlPlaneDeps())

  def test_valid_extension_deps(self):
    validator = self.BuildValidator(
        {
            'a': FakeDep('controlplane'),
            'b': FakeDep('dataplane_ext', ['foo'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
    validator.ValidateExtensionDeps('foo', '//source/extensions/foo/...')

  def test_invalid_extension_deps_wrong_category(self):
    validator = self.BuildValidator(
        {
            'a': FakeDep('controlplane'),
            'b': FakeDep('controlplane', ['foo'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
    self.assertRaises(validate.DependencyError,
                      lambda: validator.ValidateExtensionDeps('foo', '//source/extensions/foo/...'))

  def test_invalid_extension_deps_allowlist(self):
    validator = self.BuildValidator(
        {
            'a': FakeDep('controlplane'),
            'b': FakeDep('dataplane_ext', ['bar'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
    self.assertRaises(validate.DependencyError,
                      lambda: validator.ValidateExtensionDeps('foo', '//source/extensions/foo/...'))


if __name__ == '__main__':
  unittest.main()
