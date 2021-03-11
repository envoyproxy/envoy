# Modules exported from bazel

import os


# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def LoadModule(name, path):
  spec = spec_from_loader(name, SourceFileLoader(name, path))
  module = module_from_spec(spec)
  spec.loader.exec_module(module)
  return module

api_path = os.getenv("API_PATH", "external/envoy_api_canonical")

# Modules
envoy_repository_locations = LoadModule('envoy_repository_locations',
                                        'bazel/repository_locations.bzl')
repository_locations_utils = LoadModule(
    'repository_locations_utils', os.path.join(api_path, 'bazel/repository_locations_utils.bzl'))
api_repository_locations = LoadModule('api_repository_locations',
                                      os.path.join(api_path, 'bazel/repository_locations.bzl'))
