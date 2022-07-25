#!/usr/bin/env python3

from subprocess import check_output, STDOUT, CalledProcessError
import argparse
import glob
import os
import shlex
import shutil
import sys
import re

# Needed for CI to pass down bazel options.
BAZEL_BUILD_OPTIONS = shlex.split(os.environ.get('BAZEL_BUILD_OPTIONS', ''))

TARGETS = '@envoy_api//...'
IMPORT_BASE = 'github.com/envoyproxy/go-control-plane'
REPO_BASE = 'go-control-plane'
BRANCH = 'main'
MIRROR_MSG = 'Mirrored from envoyproxy/envoy @ '
USER_NAME = 'go-control-plane(Azure Pipelines)'
USER_EMAIL = 'go-control-plane@users.noreply.github.com'


def generate_protobufs(targets, output, api_repo):
    bazel_bin = check_output(['bazel', 'info', 'bazel-bin']).decode().strip()
    go_protos = check_output([
        'bazel',
        'query',
        'kind("go_proto_library", %s)' % targets,
    ]).split()

    # Each rule has the form @envoy_api//foo/bar:baz_go_proto.
    # First build all the rules to ensure we have the output files.
    # We preserve source info so comments are retained on generated code.
    try:
        check_output([
            'bazel', 'build', '-c', 'fastbuild',
            '--experimental_proto_descriptor_sets_include_source_info'
        ] + BAZEL_BUILD_OPTIONS + go_protos,
                     stderr=STDOUT)
    except CalledProcessError as e:
        print(e.output)
        raise e

    for rule in go_protos:
        # Example rule:
        # @envoy_api//envoy/config/bootstrap/v2:pkg_go_proto
        #
        # Example generated directory:
        # bazel-bin/external/envoy_api/envoy/config/bootstrap/v2/linux_amd64_stripped/pkg_go_proto%/github.com/envoyproxy/go-control-plane/envoy/config/bootstrap/v2/
        #
        # Example output directory:
        # go_out/envoy/config/bootstrap/v2
        rule_dir, proto = rule.decode().rsplit('//', 1)[1].rsplit(':', 1)

        prefix = '' if not api_repo else os.path.join('external', api_repo)
        input_dir = os.path.join(bazel_bin, prefix, rule_dir, proto + '_', IMPORT_BASE, rule_dir)
        input_files = glob.glob(os.path.join(input_dir, '*.go'))
        output_dir = os.path.join(output, rule_dir)

        # Ensure the output directory exists
        os.makedirs(output_dir, 0o755, exist_ok=True)
        for generated_file in input_files:
            shutil.copy(generated_file, output_dir)
    print('Go artifacts placed into: ' + output)


def git(repo, *args):
    cmd = ['git']
    if repo:
        cmd = cmd + ['-C', repo]
    for arg in args:
        cmd = cmd + [arg]
    return check_output(cmd).decode()


def clone_go_protobufs(repo):
    # Create a local clone of go-control-plane
    git(None, 'clone', 'git@github.com:envoyproxy/go-control-plane', repo, '-b', BRANCH)


def find_last_sync_sha(repo):
    # Determine last envoyproxy/envoy SHA in envoyproxy/go-control-plane
    last_commit = git(repo, 'log', '--grep=' + MIRROR_MSG, '-n', '1', '--format=%B').strip()
    # Initial SHA from which the APIs start syncing. Prior to that it was done manually.
    if last_commit == "":
        return 'e7f0b7176efdc65f96eb1697b829d1e6187f4502'
    m = re.search(MIRROR_MSG + '(\w+)', last_commit)
    return m.group(1)


def updated_since_sha(repo, last_sha):
    # Determine if there are changes to API since last SHA
    return git(None, 'rev-list', '%s..HEAD' % last_sha).split()


def write_revision_info(repo, sha):
    # Put a file in the generated code root containing the latest mirrored SHA
    dst = os.path.join(repo, 'envoy', 'COMMIT')
    with open(dst, 'w') as fh:
        fh.write(sha)


def sync_go_protobufs(output, repo):
    for folder in ['envoy', 'contrib']:
        # Sync generated content against repo and return true if there is a commit necessary
        dst = os.path.join(repo, folder)
        # Remove subtree in repo
        git(repo, 'rm', '-r', '--ignore-unmatch', folder)
        # Copy subtree from output to repo
        shutil.copytree(os.path.join(output, folder), dst)
        git(repo, 'add', folder)


def publish_go_protobufs(repo, sha):
    # Publish generated files with the last SHA changes to API
    git(repo, 'config', 'user.name', USER_NAME)
    git(repo, 'config', 'user.email', USER_EMAIL)
    git(repo, 'add', 'envoy')
    git(repo, 'add', 'contrib')
    git(repo, 'commit', '--allow-empty', '-s', '-m', MIRROR_MSG + sha)
    git(repo, 'push', 'origin', BRANCH)


def updated(repo):
    return len([
        f for f in git(repo, 'diff', 'HEAD', '--name-only').splitlines() if f != 'envoy/COMMIT'
    ]) > 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='Generate Go protobuf files and sync with go-control-plane')
    parser.add_argument('--sync', action='store_true')
    parser.add_argument('--output_base', default='build_go')
    parser.add_argument('--targets', default=TARGETS)
    parser.add_argument('--api_repo', default="envoy_api")
    args = parser.parse_args()

    workspace = check_output(['bazel', 'info', 'workspace']).decode().strip()
    output = os.path.join(workspace, args.output_base)
    generate_protobufs(args.targets, output, args.api_repo)
    if not args.sync:
        print('Skipping sync with go-control-plane')
        sys.exit()

    repo = os.path.join(workspace, REPO_BASE)
    clone_go_protobufs(repo)
    sync_go_protobufs(output, repo)
    last_sha = find_last_sync_sha(repo)
    changes = updated_since_sha(repo, last_sha)
    if updated(repo):
        print('Changes detected: %s' % changes)
        new_sha = changes[0]
        write_revision_info(repo, new_sha)
        publish_go_protobufs(repo, new_sha)
