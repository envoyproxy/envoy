# Lint as: python3
"""Tests for headersplit."""

import unittest
from unittest import mock
import os
from pathlib import Path
import replace_includes


class ReplaceIncludesTest(unittest.TestCase):

  def test_to_classname(self):
    # Test file name with whole path
    self.assertEqual(replace_includes.to_classname("test/mocks/server/admin_stream.h"),
                     "MockAdminStream")

    # Test file name without .h extension
    self.assertEqual(replace_includes.to_classname("cluster_mock_priority_set"),
                     "MockClusterMockPrioritySet")

  def test_to_bazelname(self):
    # Test file name with whole path
    self.assertEqual(replace_includes.to_bazelname("test/mocks/server/admin_stream.h", "server"),
                     "//test/mocks/server:admin_stream_mocks")

    # Test file name without .h extension
    self.assertEqual(replace_includes.to_bazelname("cluster_mock_priority_set", "upstream"),
                     "//test/mocks/upstream:cluster_mock_priority_set_mocks")

  class Fake_dir():
    # fake directory to test get_filenames
    def glob(sefl, _):
      return [
          Path("test/mocks/server/admin_stream.h"),
          Path("test/mocks/server/admin.h"),
          Path("test/mocks/upstream/cluster_manager.h")
      ]

  @mock.patch('replace_includes.Path', return_value=Fake_dir())
  def test_get_filenames(self, mock_Path):
    self.assertEqual(replace_includes.get_filenames('sever'), [
        'test/mocks/server/admin_stream.h', 'test/mocks/server/admin.h',
        'test/mocks/upstream/cluster_manager.h'
    ])

  def test_replace_includes(self):
    fake_source_code = """
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/context_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "test/common/http/common.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"


....useless stuff...

NiceMock<Upstream::MockClusterManager> cm_;

...uninteresting stuff..
    """
    fake_build_file = """
envoy_cc_test(
    name = "async_client_impl_test",
    srcs = ["async_client_impl_test.cc"],
    deps = [
        ":common_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/upstreams/http/generic:config",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)
    """
    os.mkdir('test')
    os.mkdir('test/mocks')
    os.mkdir('test/mocks/upstream')
    open('test/mocks/upstream/cluster_manager.h', 'a').close()
    with open('test/async_client_impl_test.cc', 'w') as f:
      f.write(fake_source_code)
    with open('test/BUILD', 'w') as f:
      f.write(fake_build_file)
    replace_includes.replace_includes('upstream')
    source_code = ""
    build_file = ""
    with open('test/async_client_impl_test.cc', 'r') as f:
      source_code = f.read()
    with open('test/BUILD', 'r') as f:
      build_file = f.read()
    self.assertEqual(source_code,
                     fake_source_code.replace('upstream/mocks', 'upstream/cluster_manager'))
    self.assertEqual(
        build_file,
        fake_build_file.replace('upstream:upstream_mocks', 'upstream:cluster_manager_mocks'))


if __name__ == '__main__':
  unittest.main()
