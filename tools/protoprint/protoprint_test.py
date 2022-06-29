#!/usr/bin/env python3

import argparse
import logging
import os
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile

from tools.protoprint.test_data import data as test_data
from tools.run_command import run_command

from envoy_repo import PATH


def path_and_filename(label):
    """Retrieve actual path and filename from bazel label

    Args:
        label: bazel label to specify target proto.

    Returns:
        actual path and filename
    """
    if label.startswith('/'):
        label = label.replace('//', '/', 1)
    elif label.startswith('@'):
        label = re.sub(r'@.*/', '/', label)
    else:
        return label
    label = label.replace(":", "/")
    splitted_label = label.split('/')
    # transforms a proto label
    # - eg `@envoy_api//foo/bar:baz.proto` -> `foo/bar`, `baz.proto`
    return '/'.join(splitted_label[:len(splitted_label) - 1])[1:], splitted_label[-1]


def golden_proto_file(tmp, path, filename, version):
    """Retrieve golden proto file path. In general, those are placed in tools/testdata/protoxform.

    Args:
        path: target proto path
        filename: target proto filename
        version: api version to specify target golden proto filename

    Returns:
        actual golden proto absolute path
    """

    return tmp.joinpath("golden").joinpath(f"{filename}.{version}.gold").absolute()


def proto_print(protoprint, descriptor, src, dst):
    """Pretty-print FileDescriptorProto to a destination file.

    Args:
        src: source path for FileDescriptorProto.
        dst: destination path for formatted proto.
    """
    print('proto_print %s -> %s' % (src, dst))
    subprocess.check_call(
        [protoprint, src, dst, descriptor, './tools/testdata/protoxform/TEST_API_VERSION'])


def result_proto_file(tmp, protoprint, descriptor, path, filename, version):
    """Retrieve result proto file path. In general, those are placed in bazel artifacts.

    Args:
        cmd: fix or freeze?
        path: target proto path
        tmp: temporary directory.
        filename: target proto filename
        version: api version to specify target result proto filename

    Returns:
        actual result proto absolute path
    """

    pkg_dir = tmp.joinpath("xformed")
    base = pkg_dir.joinpath("fix_protos").joinpath(path).joinpath(f"{filename}.{version}.proto")
    dst = pathlib.Path(filename).absolute()
    proto_print(protoprint, descriptor, str(base.absolute()), str(dst))
    return dst


def diff(result_file, golden_file):
    """Execute diff command with unified form

    Args:
        result_file: result proto file
        golden_file: golden proto file

    Returns:
        output and status code
    """
    return run_command(f"diff -u {result_file} {golden_file}")


def run(tmp, protoprint, descriptor, target, version):
    """Run main execution for protoxform test

    Args:
        tmp: path to temporary directory
        cmd: fix or freeze?
        path: target proto path
        filename: target proto filename
        version: api version to specify target result proto filename

    Returns:
        result message extracted from diff command
    """
    message = ""

    path, filename = path_and_filename(target)
    golden_path = golden_proto_file(tmp, path, filename, version)
    test_path = result_proto_file(tmp, protoprint, descriptor, path, filename, version)

    if os.stat(golden_path).st_size == 0 and not os.path.exists(test_path):
        return message

    status, stdout, stderr = diff(golden_path, test_path)

    if status != 0:
        message = '\n'.join([str(line) for line in stdout + stderr])

    return message


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("protoprint")
    parser.add_argument("descriptor")
    parser.add_argument("xformed")
    parser.add_argument("golden")
    parsed = parser.parse_args(sys.argv[1:])

    protoprint = str(pathlib.Path(parsed.protoprint).absolute())
    descriptor = str(pathlib.Path(parsed.descriptor).absolute())

    with tempfile.TemporaryDirectory() as _tmp:
        tmp = pathlib.Path(_tmp)
        xformed = tmp.joinpath("xformed")
        golden = tmp.joinpath("golden")

        with tarfile.open(parsed.xformed) as tar:
            tar.extractall(xformed)
        with tarfile.open(parsed.golden) as tar:
            tar.extractall(golden)

        os.chdir(PATH)

        messages = ""
        logging.basicConfig(format='%(message)s')
        for target in test_data:
            messages += run(tmp, protoprint, descriptor, target, 'active_or_frozen')

        if len(messages) == 0:
            logging.warning("PASS")
            sys.exit(0)
        else:
            logging.error("FAILED:\n{}".format(messages))
            sys.exit(1)


if __name__ == "__main__":
    main()
