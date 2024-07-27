#!/usr/bin/env python3

# Mangle protoxform and protoprint artifacts.

import argparse
from collections import defaultdict
import os
import pathlib
import re
import shutil
import string
import subprocess
import tarfile
import tempfile

from tools.proto_format.data import data

# These .proto import direct path prefixes are already handled by
# api_proto_package() as implicit dependencies.
API_BUILD_SYSTEM_IMPORT_PREFIXES = [
    'google/api/annotations.proto',
    'google/protobuf/',
    'google/rpc/status.proto',
    'validate/validate.proto',
]

# Each of the following contrib extensions are allowed to be in the v3 namespace. Indicate why.
CONTRIB_V3_ALLOW_LIST = [
    # Extensions moved from core to contrib.
    'envoy.extensions.filters.http.dynamo.v3',
    'envoy.extensions.filters.http.squash.v3',
    'envoy.extensions.filters.network.client_ssl_auth.v3',
    'envoy.extensions.filters.network.generic_proxy.action.v3',
    'envoy.extensions.filters.network.generic_proxy.codecs.dubbo.v3',
    'envoy.extensions.filters.network.generic_proxy.codecs.http1.v3',
    'envoy.extensions.filters.network.generic_proxy.codecs.kafka.v3',
    'envoy.extensions.filters.network.generic_proxy.matcher.v3',
    'envoy.extensions.filters.network.generic_proxy.router.v3',
    'envoy.extensions.filters.network.generic_proxy.v3',
    'envoy.extensions.filters.network.kafka_broker.v3',
    'envoy.extensions.filters.network.mysql_proxy.v3',
    'envoy.extensions.filters.network.rocketmq_proxy.v3',
]

BUILD_FILE_TEMPLATE = string.Template(
    """# DO NOT EDIT. This file is generated by tools/proto_format/proto_sync.py.

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package($fields)
""")

VERSIONING_BUILD_FILE_TEMPLATE = string.Template(
    """# DO NOT EDIT. This file is generated by tools/proto_format/proto_sync.py.

load("@rules_proto//proto:defs.bzl", "proto_library")

licenses(["notice"])  # Apache 2

# This tracks active development versions of protos.
proto_library(
    name = "active_protos",
    visibility = ["//visibility:public"],
    deps = [
$active_pkgs    ],
)

# This tracks frozen versions of protos.
proto_library(
    name = "frozen_protos",
    visibility = ["//visibility:public"],
    deps = [
$frozen_pkgs    ],
)
""")

IMPORT_REGEX = re.compile(r'import "(.*)";')
SERVICE_REGEX = re.compile(r'service \w+ {')
PACKAGE_REGEX = re.compile(r'\npackage ([a-z0-9_\.]*);')
PREVIOUS_MESSAGE_TYPE_REGEX = re.compile(r'previous_message_type\s+=\s+"([^"]*)";')


class ProtoSyncError(Exception):
    pass


class RequiresReformatError(ProtoSyncError):

    def __init__(self, message):
        # This doesnt make sense the only time it can be triggered is when you are already running this.
        super(RequiresReformatError, self).__init__(
            '%s; either run ./ci/do_ci.sh fix_proto_format or ./tools/proto_format/proto_format.sh fix to reformat.\n'
            % message)


def get_directory_from_package(package):
    """Get directory path from package name or full qualified message name

    Args:
        package: the full qualified name of package or message.
    """
    return '/'.join(s for s in package.split('.') if s and s[0].islower())


def get_destination_path(src):
    """Obtain destination path from a proto file path by reading its package statement.

    Args:
        src: source path
    """
    src_path = pathlib.Path(src)
    contents = src_path.read_text(encoding='utf8')
    matches = PACKAGE_REGEX.findall(contents)
    if len(matches) != 1:
        raise RequiresReformatError(
            f"Expect {src} has only one package declaration but has {len(matches)}\n{contents}")
    package = matches[0]
    dst_path = pathlib.Path(
        get_directory_from_package(package)).joinpath(src_path.name.split('.')[0] + ".proto")
    # contrib API files have the standard namespace but are in a contrib folder for clarity.
    # The following prepends contrib for contrib packages so we wind up with the real final path.
    if 'contrib' in src:
        if 'v3alpha' not in package and 'v4alpha' not in package and package not in CONTRIB_V3_ALLOW_LIST:
            raise ProtoSyncError(
                "contrib extension package '{}' does not use v3alpha namespace. "
                "Add to CONTRIB_V3_ALLOW_LIST with an explanation if this is on purpose.".format(
                    package))

        dst_path = pathlib.Path('contrib').joinpath(dst_path)
    # Non-contrib can not use alpha.
    if not 'contrib' in src:
        if (not 'v2alpha' in package and not 'v1alpha1' in package) and 'alpha' in package:
            raise ProtoSyncError(
                "package '{}' uses an alpha namespace. This is not allowed. Instead mark with "
                "(xds.annotations.v3.file_status).work_in_progress or related annotation.".format(
                    package))
    return dst_path


def sync_proto_file(srcs, dst):
    """Pretty-print a proto descriptor from protoxform.py Bazel cache artifacts."

    Args:
        dst_srcs: destination/sources path tuple.
    """
    assert (len(srcs) > 0)
    # If we only have one candidate source for a destination, just pretty-print.
    if len(srcs) == 1:
        src = srcs[0]
    else:
        # We should only see an active and next major version candidate from
        # previous version today.
        assert (len(srcs) == 2)
        src = [s for s in srcs if s.endswith('active_or_frozen.proto')][0]
    shutil.copy(src, dst)
    rel_dst_path = get_destination_path(src)
    return ['//%s:pkg' % str(rel_dst_path.parent)]


def get_import_deps(proto_path):
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
                    imports.append('@com_github_cncf_xds//udpa/annotations:pkg')
                    continue
                if import_path.startswith('xds/type/matcher/v3/'):
                    imports.append('@com_github_cncf_xds//xds/type/matcher/v3:pkg')
                    continue
                # Special case for handling XDS annotations.
                if import_path.startswith('xds/annotations/v3/'):
                    imports.append('@com_github_cncf_xds//xds/annotations/v3:pkg')
                    continue
                # Special case handling for XDS core.
                if import_path.startswith('xds/core/v3/'):
                    imports.append('@com_github_cncf_xds//xds/core/v3:pkg')
                    continue
                # Explicit remapping for external deps, compute paths for envoy/*.
                if import_path in data["external_proto_deps"]["imports"]:
                    imports.append(data["external_proto_deps"]["imports"][import_path])
                    continue
                if import_path.startswith('envoy/') or import_path.startswith('contrib/'):
                    # Ignore package internal imports.
                    if os.path.dirname(proto_path).endswith(os.path.dirname(import_path)):
                        continue
                    imports.append('//%s:pkg' % os.path.dirname(import_path))
                    continue
                raise ProtoSyncError(
                    'Unknown import path mapping for %s, please update the mappings in tools/proto_format/proto_sync.py.\n'
                    % import_path)
    return imports


def has_services(proto_path):
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


def build_file_contents(root, files):
    """Compute the canonical BUILD contents for an api/ proto directory.

    Args:
        root: base path to directory.
        files: a list of files in the directory.

    Returns:
        A string containing the canonical BUILD file content for root.
    """
    deps = set(sum([get_import_deps(os.path.join(root, f)) for f in files], []))
    _has_services = any(has_services(os.path.join(root, f)) for f in files)
    fields = []
    if _has_services:
        fields.append('    has_services = True,')
    if deps:
        if len(deps) == 1:
            formatted_deps = '"%s"' % list(deps)[0]
        else:
            formatted_deps = '\n' + '\n'.join(
                '        "%s",' % dep for dep in sorted(deps, key=build_order_key)) + '\n    '
        fields.append('    deps = [%s],' % formatted_deps)
    formatted_fields = '\n' + '\n'.join(fields) + '\n' if fields else ''
    return BUILD_FILE_TEMPLATE.substitute(fields=formatted_fields)


def sync_build_files(cmd, dst_root):
    """Diff or in-place update api/ BUILD files.

    Args:
        cmd: 'check' or 'fix'.
    """
    for root, dirs, files in os.walk(str(dst_root)):
        is_proto_dir = any(f.endswith('.proto') for f in files)
        if not is_proto_dir:
            continue
        build_contents = build_file_contents(root, files)
        build_path = os.path.join(root, 'BUILD')
        with open(build_path, 'w') as f:
            f.write(build_contents)


# Key sort function to achieve consistent results with buildifier.
def build_order_key(key):
    return key.replace(':', '!')


def deps_format(pkgs):
    if not pkgs:
        return ''
    return '\n'.join(
        '        "//%s:pkg",' % p.replace('.', '/')
        for p in sorted(pkgs, key=build_order_key)) + '\n'


# Find packages with a given package version status in a given API tree root.
def find_pkgs(package_version_status, api_root):
    try:
        active_files = subprocess.check_output(
            ['grep', '-l', '-r',
             'package_version_status = %s;' % package_version_status,
             api_root]).decode().strip().split('\n')
        api_protos = [f for f in active_files if f.endswith('.proto')]
    except subprocess.CalledProcessError:
        api_protos = []
    return set([os.path.dirname(p)[len(api_root) + 1:] for p in api_protos])


def format_api(mode, outfile, xformed, printed, build_file):

    with tempfile.TemporaryDirectory() as tmp:
        dst_dir = pathlib.Path(tmp)
        printed_dir = dst_dir.joinpath("printed")
        printed_dir.mkdir(parents=True)
        with tarfile.open(printed) as tar:
            tar.extractall(printed_dir)

        xformed_dir = dst_dir.joinpath("xformed")
        xformed_dir.mkdir()
        with tarfile.open(xformed) as tar:
            tar.extractall(xformed_dir)

        paths = []
        dst_src_paths = defaultdict(list)

        for label in data["proto_targets"]:
            _label = label[len('@envoy_api//'):].replace(':', '/')
            for suffix in ["active_or_frozen", "next_major_version_candidate"]:
                xpath = xformed_dir.joinpath(f"pkg/{_label}.{suffix}.proto")
                path = printed_dir.joinpath(f"{_label}.proto")

                if xpath.exists() and os.stat(xpath).st_size > 0:
                    target = dst_dir.joinpath(_label)
                    target.parent.mkdir(exist_ok=True, parents=True)
                    dst_src_paths[str(target)].append(str(path))

        for k, v in dst_src_paths.items():
            sync_proto_file(v, k)
        sync_build_files(mode, dst_dir)

        # Add the build files
        dst_dir.joinpath("BUILD").write_bytes(build_file.read_bytes())

        active_pkgs = find_pkgs('ACTIVE', str(printed_dir))
        frozen_pkgs = find_pkgs('FROZEN', str(printed_dir))

        dst_dir.joinpath("versioning").mkdir(exist_ok=True)
        dst_dir.joinpath("versioning/BUILD").write_text(
            VERSIONING_BUILD_FILE_TEMPLATE.substitute(
                active_pkgs=deps_format(active_pkgs), frozen_pkgs=deps_format(frozen_pkgs)))

        shutil.rmtree(str(printed_dir))
        shutil.rmtree(str(xformed_dir))
        with tarfile.open(outfile, "w:gz") as tar:
            tar.add(dst_dir, arcname=".")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['check', 'fix'])
    parser.add_argument('--outfile')
    parser.add_argument('--protoprinted')
    parser.add_argument('--xformed')
    parser.add_argument('--build_file')
    args = parser.parse_args()

    format_api(
        args.mode, str(pathlib.Path(args.outfile).absolute()),
        str(pathlib.Path(args.xformed).absolute()), args.protoprinted,
        pathlib.Path(args.build_file))
