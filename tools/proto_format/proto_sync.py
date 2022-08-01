#!/usr/bin/env python3

# Diff or copy pretty-printed artifacts to the source tree.

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile


def git_status(path):
    return subprocess.check_output(
        ['git', 'status', '--porcelain', str(path)], cwd=str(path)).decode()


def generate_current_api_dir(api_dir, dst_dir):
    """Helper function to generate original API repository to be compared with diff.
    This copies the original API repository and deletes file we don't want to compare.
    Args:
        api_dir: the original api directory
        dst_dir: the api directory to be compared in temporary directory
    """
    contrib_dst = dst_dir.joinpath("contrib")
    shutil.copytree(str(api_dir.joinpath("contrib")), str(contrib_dst))

    dst = dst_dir.joinpath("envoy")
    shutil.copytree(str(api_dir.joinpath("envoy")), str(dst))

    # envoy.service.auth.v2alpha exist for compatibility while we don't run in protoxform
    # so we ignore it here.
    shutil.rmtree(str(dst.joinpath("service", "auth", "v2alpha")))

    for p in dst.glob('**/*.md'):
        p.unlink()


def sync(api_root, formatted, mode, is_ci):
    api_root_path = pathlib.Path(api_root)
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = pathlib.Path(tmp)

        # a - the actual api directory found on fs
        current_api_dir = tmp_path.joinpath("a")
        current_api_dir.mkdir(0o755, True, True)
        api_root_path = pathlib.Path(api_root).absolute()
        generate_current_api_dir(api_root_path, current_api_dir)

        # b - ideally formatted version of api directory according to bazel
        dst_dir = tmp_path.joinpath("b")
        with tarfile.open(formatted) as tar:
            tar.extractall(dst_dir)

        # These support files are handled manually.
        for f in ['envoy/annotations/resource.proto', 'envoy/annotations/deprecation.proto',
                  'envoy/annotations/BUILD']:
            copy_dst_dir = pathlib.Path(dst_dir, os.path.dirname(f))
            copy_dst_dir.mkdir(exist_ok=True, parents=True)
            shutil.copy(str(pathlib.Path(api_root, f)), str(copy_dst_dir))

        diff = subprocess.run(['diff', '-Npur', "a", "b"], cwd=tmp, stdout=subprocess.PIPE).stdout

        if diff.strip():
            if mode == "check":
                print(
                    "Please apply following patch to directory '{}'".format(api_root),
                    file=sys.stderr)
                print(diff.decode(), file=sys.stderr)
                sys.exit(1)
            if mode == "fix":
                _git_status = git_status(api_root_path)
                if _git_status:
                    print('git status indicates a dirty API tree:\n%s' % _git_status)
                    print(
                        'Proto formatting may overwrite or delete files in the above list with no git backup.'
                    )
                    if not is_ci and input('Continue? [yN] ').strip().lower() != 'y':
                        sys.exit(1)
                src_files = set(
                    str(p.relative_to(current_api_dir)) for p in current_api_dir.rglob('*'))
                dst_files = set(str(p.relative_to(dst_dir)) for p in dst_dir.rglob('*'))
                deleted_files = src_files.difference(dst_files)
                if deleted_files:
                    print('The following files will be deleted: %s' % sorted(deleted_files))
                    print(
                        'If this is not intended, please see https://github.com/envoyproxy/envoy/blob/main/api/STYLE.md#adding-an-extension-configuration-to-the-api.'
                    )
                    if not is_ci and input('Delete files? [yN] ').strip().lower() != 'y':
                        sys.exit(1)
                    else:
                        subprocess.run(['patch', '-p1'],
                                       input=diff,
                                       cwd=str(api_root_path.resolve()))
                else:
                    subprocess.run(['patch', '-p1'], input=diff, cwd=str(api_root_path.resolve()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--formatted')
    parser.add_argument('--mode', choices=['check', 'fix'])
    parser.add_argument('--api_root', default='./api')
    parser.add_argument('--ci', action="store_true", default=False)
    args = parser.parse_args()

    sync(args.api_root, str(pathlib.Path(args.formatted).absolute()), args.mode, args.ci)
