#!/usr/bin/env python3
from subprocess import check_output
from subprocess import check_call

import glob
import os
import shutil
import sys

targets = '@envoy_api//...'
import_base = 'github.com/envoyproxy/go-control-plane'
output_base = 'build_go'
repo_base = 'go-control-plane'

def generateProtobufs(output):
    bazel_bin = check_output(['bazel', 'info', 'bazel-bin']).decode().strip()
    go_protos = check_output([
        'bazel',
        'query',
        'kind("go_proto_library", %s)' % targets,
    ]).split()

    # Each rule has the form @envoy_api//foo/bar:baz_go_proto.
    # First build all the rules to ensure we have the output files.
    check_call(['bazel', 'build', '-c', 'fastbuild'] + go_protos)

    shutil.rmtree(output, ignore_errors=True)
    for rule in go_protos:
      # Example rule:
      # @envoy_api//envoy/config/bootstrap/v2:pkg_go_proto
      #
      # Example generated directory:
      # bazel-bin/external/envoy_api/envoy/config/bootstrap/v2/linux_amd64_stripped/pkg_go_proto%/github.com/envoyproxy/go-control-plane/envoy/config/bootstrap/v2/
      #
      # Example output directory:
      # go_out/envoy/config/bootstrap/v2
      rule_dir, proto = rule.decode()[len('@envoy_api//'):].rsplit(':', 1)

      input_dir = os.path.join(bazel_bin, 'external', 'envoy_api', rule_dir, 'linux_amd64_stripped',
                               proto + '%', import_base, rule_dir)
      input_files = glob.glob(os.path.join(input_dir, '*.go'))
      output_dir = os.path.join(output, rule_dir)

      # Ensure the output directory exists
      os.makedirs(output_dir, 0o755, exist_ok=True)
      for generated_file in input_files:
        shutil.copy(generated_file, output_dir)
    print('Go artifacts placed into: ' + output)

def cloneGoProtobufs(repo):
    shutil.rmtree(repo, ignore_errors=True)
    check_call(['git', 'clone', 'git@git:envoyproxy/go-control-plane', repo])
    check_call(['git', '-C', repo, 'config', 'user.name', 'go-control-plane(CircleCI)'])
    check_call(['git', '-C', repo, 'config', 'user.email', 'go-control-plane@users.noreply.github.com'])
    check_call(['git', '-C', repo, 'fetch'])
    check_call(['git', '-C', repo, 'checkout', '-B', 'sync', 'origin/master'])

# Sync generated content against repo and return true if there is a commit necessary
def syncGoProtobufs(output, repo):
    dst = os.path.join(repo, 'envoy')
    # Remove subtree at envoy in repo
    shutil.rmtree(dst, ignore_errors=True)
    # Copy subtree at envoy from output to repo
    shutil.copytree(os.path.join(output, 'envoy'), dst)

def publishGoProtobufs(repo):
    # TODO
    sha = '38b926c63f347a70c933e0854ee9f31b1d2e85ce'
    check_call(['git', '-C', repo, 'add', 'envoy'])
    check_call(['git', '-C', repo, 'commit', '-m', 'Mirrored from envoyproxy/envoy @ %s' % sha]) 
    check_call(['git', '-C', repo, 'push', 'origin', 'sync'])

if __name__ == "__main__":
    workspace = check_output(['bazel', 'info', 'workspace']).decode().strip()
    output = os.path.join(workspace, output_base)
    generateProtobufs(output)
    repo = os.path.join(workspace, repo_base)
    cloneGoProtobufs(repo)
    syncGoProtobufs(output, repo)
    publishGoProtobufs(repo)
