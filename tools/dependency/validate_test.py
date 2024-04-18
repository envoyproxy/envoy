#!/usr/bin/env python3
"""Tests for validate.py"""

import asyncio
import unittest

from tools.dependency import validate


class FakeDependencyInfo(object):
    """validate.DependencyInfo fake."""

    def __init__(self, deps):
        self._deps = deps

    def deps_by_use_category(self, use_category):
        return set(n for n, m in self._deps.items() if use_category in m['use_category'])

    def get_metadata(self, dependency):
        return self._deps.get(dependency)


class FakeBuildGraph(object):
    """validate.BuildGraph fake."""

    def __init__(self, reachable_deps, extensions):
        self._reachable_deps = reachable_deps
        self._extensions = extensions

    async def query_external_deps(self, *targets, exclude=None):
        result = set(sum((self._reachable_deps.get(t, []) for t in targets), []))
        if exclude:
            result = result - await self.query_external_deps(*exclude)
        return result

    def list_extensions(self):
        return self._extensions


def fake_dep(use_category, extensions=[]):
    return {'use_category': use_category, 'extensions': extensions}


class ValidateTest(unittest.TestCase):

    def build_validator(self, deps, reachable_deps, extensions=[]):
        return validate.Validator(
            FakeDependencyInfo(deps), FakeBuildGraph(reachable_deps, extensions))

    def test_valid_build_graph_structure(self):
        validator = self.build_validator({}, {
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a'],
            '//source/extensions/...': ['b'],
            '//source/...': ['a', 'b']
        })
        asyncio.run(validator.validate_build_graph_structure())

    def test_invalid_build_graph_structure(self):
        validator = self.build_validator({}, {
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a'],
            '//source/extensions/...': ['b'],
            '//source/...': ['a', 'b', 'c']
        })
        self.assertRaises(
            validate.DependencyError,
            lambda: asyncio.run(validator.validate_build_graph_structure()))

    def test_valid_test_only_deps(self):
        validator = self.build_validator({'a': fake_dep('dataplane_core')}, {'//source/...': ['a']})
        asyncio.run(validator.validate_test_only_deps())
        validator = self.build_validator({'a': fake_dep('test_only')},
                                         {'//test/...': ['a', 'b__pip3']})
        asyncio.run(validator.validate_test_only_deps())

    def test_invalid_test_only_deps(self):
        validator = self.build_validator({'a': fake_dep('test_only')}, {'//source/...': ['a']})
        self.assertRaises(
            validate.DependencyError, lambda: asyncio.run(validator.validate_test_only_deps()))
        validator = self.build_validator({'a': fake_dep('test_only')}, {'//test/...': ['b']})
        self.assertRaises(
            validate.DependencyError, lambda: asyncio.run(validator.validate_test_only_deps()))

    def test_valid_dataplane_core_deps(self):
        validator = self.build_validator({'a': fake_dep('dataplane_core')},
                                         {'//source/common/http/...': ['a']})
        asyncio.run(validator.validate_data_plane_core_deps())

    def test_invalid_dataplane_core_deps(self):
        validator = self.build_validator({'a': fake_dep('controlplane')},
                                         {'//source/common/http/...': ['a']})
        self.assertRaises(
            validate.DependencyError,
            lambda: asyncio.run(validator.validate_data_plane_core_deps()))

    def test_valid_controlplane_deps(self):
        validator = self.build_validator({'a': fake_dep('controlplane')},
                                         {'//source/common/config/...': ['a']})
        asyncio.run(validator.validate_control_plane_deps())

    def test_invalid_controlplane_deps(self):
        validator = self.build_validator({'a': fake_dep('other')},
                                         {'//source/common/config/...': ['a']})
        self.assertRaises(
            validate.DependencyError, lambda: asyncio.run(validator.validate_control_plane_deps()))

    def test_valid_extension_deps(self):
        validator = self.build_validator({
            'a': fake_dep('controlplane'),
            'b': fake_dep('dataplane_ext', ['foo'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
        asyncio.run(validator.validate_extension_deps('foo', '//source/extensions/foo/...'))

    def test_invalid_extension_deps_wrong_category(self):
        validator = self.build_validator({
            'a': fake_dep('controlplane'),
            'b': fake_dep('controlplane', ['foo'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
        self.assertRaises(
            validate.DependencyError, lambda: asyncio.run(
                validator.validate_extension_deps('foo', '//source/extensions/foo/...')))

    def test_invalid_extension_deps_allowlist(self):
        validator = self.build_validator({
            'a': fake_dep('controlplane'),
            'b': fake_dep('dataplane_ext', ['bar'])
        }, {
            '//source/extensions/foo/...': ['a', 'b'],
            '//source/exe:envoy_main_common_with_core_extensions_lib': ['a']
        })
        self.assertRaises(
            validate.DependencyError, lambda: asyncio.run(
                validator.validate_extension_deps('foo', '//source/extensions/foo/...')))


if __name__ == '__main__':
    unittest.main()
