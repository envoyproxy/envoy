#!/usr/bin/env python3

# Generate an extension database, a JSON file mapping from qualified well known
# extension name to metadata derived from the envoy_cc_extension target.

import json
import os
import pathlib
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


def GetExtensionMetadata(target):
  r = subprocess.run(
      [BUILDOZER_PATH, '-stdout', 'print security_posture status undocumented', target],
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE)
  security_posture, status, undocumented = r.stdout.decode('utf-8').strip().split(' ')
  if IsMissing(security_posture):
    raise ExtensionDbError('Missing security posture for %s' % target)
  return {
      'security_posture': security_posture,
      'undocumented': False if IsMissing(undocumented) else bool(undocumented),
      'status': 'stable' if IsMissing(status) else status,
  }


if __name__ == '__main__':
  output_path = sys.argv[1]
  extension_db = {}
  for extension, target in extensions_build_config.EXTENSIONS.items():
    extension_db[extension] = GetExtensionMetadata(target)

  pathlib.Path(output_path).write_text(json.dumps(extension_db))
