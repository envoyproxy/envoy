#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path

from tools.api_proto_breaking_change_detector.detector import BufWrapper

import envoy_repo

API_DIR = Path(envoy_repo.PATH).joinpath("api")
GIT_PATH = Path(envoy_repo.PATH).joinpath(".git")
CONFIG_FILE_LOC = Path(API_DIR, "buf.yaml")


def detect_breaking_changes_git(path_to_buf, ref):
    """Returns True if breaking changes were detected in the api folder"""
    detector = BufWrapper(
        API_DIR,
        buf_path=path_to_buf,
        config_file_loc=CONFIG_FILE_LOC,
        git_ref=ref,
        git_path=GIT_PATH,
        subdir="api")
    detector.run_detector()
    breaking = detector.is_breaking()

    if breaking:
        print('Breaking changes detected in api protobufs:')
        for i, breaking_change in enumerate(detector.get_breaking_changes()):
            print(f'\t{i}: {breaking_change}')
        print("ERROR: non-backwards-compatible changes detected in api protobufs.")
    return breaking


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description=
        'Tool to detect breaking changes in api protobufs and enforce backwards compatibility.')
    parser.add_argument('buf_path', type=str, help='path to buf binary')
    parser.add_argument(
        'git_ref', type=str, help='git reference to check against for breaking changes')
    args = parser.parse_args()
    buf_path = os.path.abspath(args.buf_path)
    os.chdir(envoy_repo.PATH)
    exit_status = detect_breaking_changes_git(buf_path, args.git_ref)
    sys.exit(exit_status)
