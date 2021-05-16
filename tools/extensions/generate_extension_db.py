#!/usr/bin/env python3

# Generate an extension database, a JSON file mapping from qualified well known
# extension name to metadata derived from the envoy_cc_extension target.

import json
import os
import pathlib
import re
import sys

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

# source/extensions/extensions_build_config.bzl must have a .bzl suffix for Starlark
# import, so we are forced to do this workaround.
_extensions_build_config_spec = spec_from_loader(
    'extensions_build_config',
    SourceFileLoader('extensions_build_config', 'source/extensions/extensions_build_config.bzl'))
extensions_build_config = module_from_spec(_extensions_build_config_spec)
_extensions_build_config_spec.loader.exec_module(extensions_build_config)


class ExtensionDbError(Exception):
    pass


def num_read_filters_fuzzed():
    data = pathlib.Path(
        'test/extensions/filters/network/common/fuzz/uber_per_readfilter.cc').read_text()
    # Hack-ish! We only search the first 50 lines to capture the filters in filterNames().
    return len(re.findall('NetworkFilterNames::get()', ''.join(data.splitlines()[:50])))


def num_robust_to_downstream_network_filters(db):
    # Count number of network filters robust to untrusted downstreams.
    return len([
        ext for ext, data in db.items()
        if 'network' in ext and data['security_posture'] == 'robust_to_untrusted_downstream'
    ])


def validate_extension(name, extension):
    if not extension.get("security_posture"):
        raise ExtensionDbError(
            f"Missing security posture for {name}. "
            "Please make sure the target is an envoy_cc_extension and security_posture is set")

    if not extension.get("categories"):
        raise ExtensionDbError(
            f"Missing extension categories for {name}. "
            "Please make sure the target is an envoy_cc_extension and category is set")


if __name__ == '__main__':
    extension_db = {}
    # Include all extensions from source/extensions/extensions_build_config.bzl
    for name, extension in extensions_build_config.EXTENSIONS.items():
        # //source/extensions/common/crypto:utility_lib has been added to `EXTENSIONS`
        # but unlike the `builtin` extensions was not originally included here so has been excluded
        if extension["categories"] == ["DELIBERATELY_OMITTED"]:
            continue
        extension_db[name] = dict(undocumented=False, status="stable")
        extension_db[name].update(extension)
        validate_extension(name, extension_db[name])
    if num_robust_to_downstream_network_filters(extension_db) != num_read_filters_fuzzed():
        raise ExtensionDbError(
            'Check that all network filters robust against untrusted'
            'downstreams are fuzzed by adding them to filterNames() in'
            'test/extensions/filters/network/common/uber_per_readfilter.cc')

    sys.stdout.write(json.dumps(extension_db))
