# Modules exported from bazel

import os
from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader


# Shared Starlark/Python files must have a .bzl suffix for Starlark import, so
# we are forced to do this workaround.
def load_module(name, path):
    spec = spec_from_loader(name, SourceFileLoader(name, path))
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# this is the relative path in a bazel build
# to call this module outside of a bazel build set the `API_PATH` first,
# for example, if running from the envoy repo root: `export API_PATH=api/`
api_path = os.getenv("API_PATH", "external/envoy_api")

# Modules
envoy_repository_locations = load_module(
    'envoy_repository_locations', 'bazel/repository_locations.bzl')
repository_locations_utils = load_module(
    'repository_locations_utils', os.path.join(api_path, 'bazel/repository_locations_utils.bzl'))
api_repository_locations = load_module(
    'api_repository_locations', os.path.join(api_path, 'bazel/repository_locations.bzl'))
