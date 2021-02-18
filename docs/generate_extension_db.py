#!/usr/bin/env python3

# Generate an extension database, a JSON file mapping from qualified well known
# extension name to metadata derived from the envoy_cc_extension target.

import ast
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

BUILDOZER_PATH = os.getenv("BUILDOZER_BIN") or (os.path.expandvars("$GOPATH/bin/buildozer") if
                                                os.getenv("GOPATH") else shutil.which("buildozer"))

# source/extensions/extensions_build_config.bzl must have a .bzl suffix for Starlark
# import, so we are forced to do this workaround.
_extensions_build_config_spec = spec_from_loader(
    'extensions_build_config',
    SourceFileLoader('extensions_build_config', 'source/extensions/extensions_build_config.bzl'))
extensions_build_config = module_from_spec(_extensions_build_config_spec)
_extensions_build_config_spec.loader.exec_module(extensions_build_config)


class ExtensionDbError(Exception):
  pass


def IsMissing(value):
  return value == '(missing)'


def NumReadFiltersFuzzed():
  data = pathlib.Path(
      'test/extensions/filters/network/common/fuzz/uber_per_readfilter.cc').read_text()
  # Hack-ish! We only search the first 50 lines to capture the filters in filterNames().
  return len(re.findall('NetworkFilterNames::get()', ''.join(data.splitlines()[:50])))


def NumRobustToDownstreamNetworkFilters(db):
  # Count number of network filters robust to untrusted downstreams.
  return len([
      ext for ext, data in db.items()
      if 'network' in ext and data['security_posture'] == 'robust_to_untrusted_downstream'
  ])


def GetExtensionMetadata(target):
  if not BUILDOZER_PATH:
    raise ExtensionDbError('Buildozer not found!')
  r = subprocess.run(
      [BUILDOZER_PATH, '-stdout', 'print security_posture status undocumented category', target],
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE)
  rout = r.stdout.decode('utf-8').strip().split(' ')
  security_posture, status, undocumented = rout[:3]
  categories = ' '.join(rout[3:])
  if IsMissing(security_posture):
    raise ExtensionDbError(
        'Missing security posture for %s.  Please make sure the target is an envoy_cc_extension and security_posture is set'
        % target)
  if IsMissing(categories):
    raise ExtensionDbError(
        'Missing extension category for %s. Please make sure the target is an envoy_cc_extension and category is set'
        % target)
  # evaluate tuples/lists
  # wrap strings in a list
  categories = (ast.literal_eval(categories) if
                ('[' in categories or '(' in categories) else [categories])
  return {
      'security_posture': security_posture,
      'undocumented': False if IsMissing(undocumented) else bool(undocumented),
      'status': 'stable' if IsMissing(status) else status,
      'categories': categories,
  }


if __name__ == '__main__':
  output_path = sys.argv[1]
  extension_db = {}
  # Include all extensions from source/extensions/extensions_build_config.bzl
  all_extensions = {}
  all_extensions.update(extensions_build_config.EXTENSIONS)
  for extension, target in all_extensions.items():
    extension_db[extension] = GetExtensionMetadata(target)
  if NumRobustToDownstreamNetworkFilters(extension_db) != NumReadFiltersFuzzed():
    raise ExtensionDbError('Check that all network filters robust against untrusted'
                           'downstreams are fuzzed by adding them to filterNames() in'
                           'test/extensions/filters/network/common/uber_per_readfilter.cc')
  # The TLS and generic upstream extensions are hard-coded into the build, so
  # not in source/extensions/extensions_build_config.bzl
  extension_db['envoy.transport_sockets.tls'] = GetExtensionMetadata(
      '//source/extensions/transport_sockets/tls:config')
  extension_db['envoy.upstreams.http.generic'] = GetExtensionMetadata(
      '//source/extensions/upstreams/http/generic:config')
  extension_db['envoy.upstreams.tcp.generic'] = GetExtensionMetadata(
      '//source/extensions/upstreams/tcp/generic:config')
  extension_db['envoy.upstreams.http.http_protocol_options'] = GetExtensionMetadata(
      '//source/extensions/upstreams/http:config')

  pathlib.Path(output_path).write_text(json.dumps(extension_db))
