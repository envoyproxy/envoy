"""Tests for generate_listeners."""

import os
import sys

# Workaround for https://github.com/bazelbuild/rules_python/issues/1221
sys.path += [os.path.dirname(__file__)]

import generate_listeners

if __name__ == "__main__":
    srcdir = os.path.join(os.getenv("TEST_SRCDIR"), 'envoy_api')
    generate_listeners.generate_listeners(
        os.path.join(srcdir, "examples/service_envoy/listeners.pb"), "/dev/stdout", "/dev/stdout",
        iter([os.path.join(srcdir, "examples/service_envoy/http_connection_manager.pb")]))
