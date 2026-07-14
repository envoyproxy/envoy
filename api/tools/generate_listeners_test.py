"""Tests for generate_listeners."""

import os
import sys

# Workaround for https://github.com/bazelbuild/rules_python/issues/1221
sys.path += [os.path.dirname(__file__)]

import generate_listeners

if __name__ == "__main__":
    test_srcdir = os.getenv("TEST_SRCDIR")
    # In bzlmod, the main repository is '_main', in WORKSPACE it's 'envoy_api'
    # Try both to support both build systems
    for workspace_name in ['_main', 'envoy_api']:
        candidate_dir = os.path.join(test_srcdir, workspace_name)
        if os.path.isdir(candidate_dir):
            srcdir = candidate_dir
            break
    else:
        # Fallback to _main if neither exists (shouldn't happen)
        srcdir = os.path.join(test_srcdir, '_main')

    generate_listeners.generate_listeners(
        os.path.join(srcdir, "examples/service_envoy/listeners.pb"), "/dev/stdout", "/dev/stdout",
        iter([os.path.join(srcdir, "examples/service_envoy/http_connection_manager.pb")]))
