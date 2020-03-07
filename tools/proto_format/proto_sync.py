#!/usr/bin/env python3

# Diff or copy protoxform artifacts from Bazel cache back to the source tree.

import argparse
import os
import pathlib
import re
import shutil
import string
import subprocess
import sys
import tempfile

from api_proto_plugin import utils

from importlib.util import spec_from_loader, module_from_spec
from importlib.machinery import SourceFileLoader

# api/bazel/external_protos_deps.bzl must have a .bzl suffix for Starlark
# import, so we are forced to this workaround.
_external_proto_deps_spec = spec_from_loader(
    'external_proto_deps',
    SourceFileLoader('external_proto_deps', 'api/bazel/external_proto_deps.bzl'))
external_proto_deps = module_from_spec(_external_proto_deps_spec)
_external_proto_deps_spec.loader.exec_module(external_proto_deps)

# These .proto import direct path prefixes are already handled by
# api_proto_package() as implicit dependencies.
API_BUILD_SYSTEM_IMPORT_PREFIXES = [
    'google/api/annotations.proto',
    'google/protobuf/',
    'google/rpc/status.proto',
    'validate/validate.proto',
]

BUILD_FILE_TEMPLATE = string.Template(
    """# DO NOT EDIT. This file is generated by tools/proto_sync.py.

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package($fields)
""")

IMPORT_REGEX = re.compile('import "(.*)";')
SERVICE_REGEX = re.compile('service \w+ {')
PACKAGE_REGEX = re.compile('\npackage ([^="]*);')
PREVIOUS_MESSAGE_TYPE_REGEX = re.compile(r'previous_message_type\s+=\s+"([^"]*)";')


class ProtoSyncError(Exception):
  pass


class RequiresReformatError(ProtoSyncError):

  def __init__(self, message):
    super(RequiresReformatError, self).__init__(
        '%s; either run ./ci/do_ci.sh fix_format or ./tools/proto_format/proto_format.sh fix to reformat.\n'
        % message)


def GetDirectoryFromPackage(package):
  """Get directory path from package name or full qualified message name

  Args:
    package: the full qualified name of package or message.
  """
  return '/'.join(s for s in package.split('.') if s and s[0].islower())


def GetDestinationPath(src):
  """Obtain destination path from a proto file path by reading its package statement.

  Args:
    src: source path
  """
  src_path = pathlib.Path(src)
  contents = src_path.read_text(encoding='utf8')
  matches = re.findall(PACKAGE_REGEX, contents)
  if len(matches) != 1:
    raise RequiresReformatError("Expect {} has only one package declaration but has {}".format(
        src, len(matches)))
  return pathlib.Path(GetDirectoryFromPackage(
      matches[0])).joinpath(src_path.name.split('.')[0] + ".proto")


def SyncProtoFile(cmd, src, dst_root):
  """Diff or in-place update a single proto file from protoxform.py Bazel cache artifacts."

  Args:
    cmd: 'check' or 'fix'.
    src: source path.
  """
  # Skip empty files, this indicates this file isn't modified in this version.
  if os.stat(src).st_size == 0:
    return []
  rel_dst_path = GetDestinationPath(src)
  dst = dst_root.joinpath(rel_dst_path)
  dst.parent.mkdir(0o755, True, True)
  shutil.copyfile(src, str(dst))
  return ['//%s:pkg' % str(rel_dst_path.parent)]


def GetImportDeps(proto_path):
  """Obtain the Bazel dependencies for the import paths from a .proto file.

  Args:
    proto_path: path to .proto.

  Returns:
    A list of Bazel targets reflecting the imports in the .proto at proto_path.
  """
  imports = []
  with open(proto_path, 'r', encoding='utf8') as f:
    for line in f:
      match = re.match(IMPORT_REGEX, line)
      if match:
        import_path = match.group(1)
        # We can ignore imports provided implicitly by api_proto_package().
        if any(import_path.startswith(p) for p in API_BUILD_SYSTEM_IMPORT_PREFIXES):
          continue
        # Special case handling for UDPA annotations.
        if import_path.startswith('udpa/annotations/'):
          imports.append('@com_github_cncf_udpa//udpa/annotations:pkg')
          continue
        # Explicit remapping for external deps, compute paths for envoy/*.
        if import_path in external_proto_deps.EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP:
          imports.append(external_proto_deps.EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP[import_path])
          continue
        if import_path.startswith('envoy/'):
          # Ignore package internal imports.
          if os.path.dirname(proto_path).endswith(os.path.dirname(import_path)):
            continue
          imports.append('//%s:pkg' % os.path.dirname(import_path))
          continue
        raise ProtoSyncError(
            'Unknown import path mapping for %s, please update the mappings in tools/proto_sync.py.\n'
            % import_path)
  return imports


def GetPreviousMessageTypeDeps(proto_path):
  """Obtain the Bazel dependencies for the previous version of messages in a .proto file.

  We need to link in earlier proto descriptors to support Envoy reflection upgrades.

  Args:
    proto_path: path to .proto.

  Returns:
    A list of Bazel targets reflecting the previous message types in the .proto at proto_path.
  """
  contents = pathlib.Path(proto_path).read_text(encoding='utf8')
  matches = re.findall(PREVIOUS_MESSAGE_TYPE_REGEX, contents)
  deps = []
  for m in matches:
    target = '//%s:pkg' % GetDirectoryFromPackage(m)
    deps.append(target)
  return deps


def HasServices(proto_path):
  """Does a .proto file have any service definitions?

  Args:
    proto_path: path to .proto.

  Returns:
    True iff there are service definitions in the .proto at proto_path.
  """
  with open(proto_path, 'r', encoding='utf8') as f:
    for line in f:
      if re.match(SERVICE_REGEX, line):
        return True
  return False


# Key sort function to achieve consistent results with buildifier.
def BuildOrderKey(key):
  return key.replace(':', '!')


def BuildFileContents(root, files):
  """Compute the canonical BUILD contents for an api/ proto directory.

  Args:
    root: base path to directory.
    files: a list of files in the directory.

  Returns:
    A string containing the canonical BUILD file content for root.
  """
  import_deps = set(sum([GetImportDeps(os.path.join(root, f)) for f in files], []))
  history_deps = set(sum([GetPreviousMessageTypeDeps(os.path.join(root, f)) for f in files], []))
  deps = import_deps.union(history_deps)
  has_services = any(HasServices(os.path.join(root, f)) for f in files)
  fields = []
  if has_services:
    fields.append('    has_services = True,')
  if deps:
    if len(deps) == 1:
      formatted_deps = '"%s"' % list(deps)[0]
    else:
      formatted_deps = '\n' + '\n'.join(
          '        "%s",' % dep for dep in sorted(deps, key=BuildOrderKey)) + '\n    '
    fields.append('    deps = [%s],' % formatted_deps)
  formatted_fields = '\n' + '\n'.join(fields) + '\n' if fields else ''
  return BUILD_FILE_TEMPLATE.substitute(fields=formatted_fields)


def SyncBuildFiles(cmd, dst_root):
  """Diff or in-place update api/ BUILD files.

  Args:
    cmd: 'check' or 'fix'.
  """
  for root, dirs, files in os.walk(str(dst_root)):
    is_proto_dir = any(f.endswith('.proto') for f in files)
    if not is_proto_dir:
      continue
    build_contents = BuildFileContents(root, files)
    build_path = os.path.join(root, 'BUILD')
    with open(build_path, 'w') as f:
      f.write(build_contents)


def GenerateCurrentApiDir(api_dir, dst_dir):
  """Helper function to generate original API repository to be compared with diff.
  This copies the original API repository and deletes file we don't want to compare.

  Args:
    api_dir: the original api directory
    dst_dir: the api directory to be compared in temporary directory
  """
  dst = dst_dir.joinpath("envoy")
  shutil.copytree(str(api_dir.joinpath("envoy")), str(dst))

  for p in dst.glob('**/*.md'):
    p.unlink()
  # envoy.service.auth.v2alpha exist for compatibility while we don't run in protoxform
  # so we ignore it here.
  shutil.rmtree(str(dst.joinpath("service", "auth", "v2alpha")))


def Sync(api_root, mode, labels, shadow):
  pkg_deps = []
  with tempfile.TemporaryDirectory() as tmp:
    dst_dir = pathlib.Path(tmp).joinpath("b")
    for label in labels:
      pkg_deps += SyncProtoFile(mode, utils.BazelBinPathForOutputArtifact(label, '.v2.proto'),
                                dst_dir)
      pkg_deps += SyncProtoFile(
          mode,
          utils.BazelBinPathForOutputArtifact(
              label, '.v3.envoy_internal.proto' if shadow else '.v3.proto'), dst_dir)
    SyncBuildFiles(mode, dst_dir)

    current_api_dir = pathlib.Path(tmp).joinpath("a")
    current_api_dir.mkdir(0o755, True, True)
    api_root_path = pathlib.Path(api_root)
    GenerateCurrentApiDir(api_root_path, current_api_dir)

    # These support files are handled manually.
    for f in [
        'envoy/annotations/resource.proto', 'envoy/annotations/deprecation.proto',
        'envoy/annotations/BUILD'
    ]:
      copy_dst_dir = pathlib.Path(dst_dir, os.path.dirname(f))
      copy_dst_dir.mkdir(exist_ok=True)
      shutil.copy(str(pathlib.Path(api_root, f)), str(copy_dst_dir))

    diff = subprocess.run(['diff', '-Npur', "a", "b"], cwd=tmp, stdout=subprocess.PIPE).stdout

    if diff.strip():
      if mode == "check":
        print("Please apply following patch to directory '{}'".format(api_root), file=sys.stderr)
        print(diff.decode(), file=sys.stderr)
        sys.exit(1)
      if mode == "fix":
        src_files = set(str(p.relative_to(current_api_dir)) for p in current_api_dir.rglob('*'))
        dst_files = set(str(p.relative_to(dst_dir)) for p in dst_dir.rglob('*'))
        deleted_files = src_files.difference(dst_files)
        if deleted_files:
          print('The following files will be deleted: %s' % sorted(deleted_files))
          print(
              'If this is not intended, please see https://github.com/envoyproxy/envoy/blob/master/api/STYLE.md#adding-an-extension-configuration-to-the-api.'
          )
          if input('Delete files? [yN] ').strip().lower() == 'y':
            subprocess.run(['patch', '-p1'], input=diff, cwd=str(api_root_path.resolve()))
          else:
            sys.exit(1)
        else:
          subprocess.run(['patch', '-p1'], input=diff, cwd=str(api_root_path.resolve()))


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('--mode', choices=['check', 'fix'])
  parser.add_argument('--api_root', default='./api')
  parser.add_argument('--api_shadow_root', default='./generated_api_shadow')
  parser.add_argument('labels', nargs='*')
  args = parser.parse_args()

  Sync(args.api_root, args.mode, args.labels, False)
  Sync(args.api_shadow_root, args.mode, args.labels, True)
