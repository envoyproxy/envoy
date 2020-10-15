# Utilities for reasoning about dependencies.

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader


# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def LoadModule(name, path):
  spec = spec_from_loader(name, SourceFileLoader(name, path))
  module = module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


envoy_repository_locations = LoadModule('envoy_repository_locations',
                                        'bazel/repository_locations.bzl')
api_repository_locations = LoadModule('api_repository_locations',
                                      'api/bazel/repository_locations.bzl')
repository_locations_utils = LoadModule('repository_locations_utils',
                                        'api/bazel/repository_locations_utils.bzl')


def RepositoryLocations():
  spec_loader = repository_locations_utils.load_repository_locations_spec
  locations = spec_loader(envoy_repository_locations.REPOSITORY_LOCATIONS_SPEC)
  locations.update(spec_loader(api_repository_locations.REPOSITORY_LOCATIONS_SPEC))
  return locations
