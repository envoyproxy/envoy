# Lint as: python3
"""Tests for replace_includes."""

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

  class FakeDir():
    # fake directory to test get_filenames
    def glob(self, _):
      return [
          Path("test/mocks/server/admin_stream.h"),
          Path("test/mocks/server/admin.h"),
          Path("test/mocks/upstream/cluster_manager.h")
      ]

  @mock.patch("replace_includes.Path", return_value=FakeDir())
  def test_get_filenames(self, mock_Path):
    self.assertEqual(replace_includes.get_filenames("sever"), [
        "test/mocks/server/admin_stream.h", "test/mocks/server/admin.h",
        "test/mocks/upstream/cluster_manager.h"
    ])

  def test_replace_includes(self):
    fake_source_code = open("tools/envoy_headersplit/code_corpus/fake_source_code.cc", "r").read()
    fake_build_file = open("tools/envoy_headersplit/code_corpus/fake_build", "r").read()
    os.mkdir("test")
    os.mkdir("test/mocks")
    os.mkdir("test/mocks/upstream")
    open("test/mocks/upstream/cluster_manager.h", "a").close()
    with open("test/async_client_impl_test.cc", "w") as f:
      f.write(fake_source_code)
    with open("test/BUILD", "w") as f:
      f.write(fake_build_file)
    replace_includes.replace_includes("upstream")
    source_code = ""
    build_file = ""
    with open("test/async_client_impl_test.cc", "r") as f:
      source_code = f.read()
    with open("test/BUILD", "r") as f:
      build_file = f.read()
    self.assertEqual(source_code,
                     fake_source_code.replace("upstream/mocks", "upstream/cluster_manager"))
    self.assertEqual(
        build_file,
        fake_build_file.replace("upstream:upstream_mocks", "upstream:cluster_manager_mocks"))


if __name__ == "__main__":
  unittest.main()
