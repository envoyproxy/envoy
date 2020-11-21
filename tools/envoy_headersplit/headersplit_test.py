# Lint as: python3
"""Tests for headersplit."""

import headersplit
import io
import os
import subprocess
import sys
import unittest

# libclang imports
import clang.cindex
from clang.cindex import TranslationUnit, Index, CursorKind


class HeadersplitTest(unittest.TestCase):
  # A header contains a simple class print hello world
  source_code_hello_world = open("tools/envoy_headersplit/code_corpus/hello.h", "r").read()
  # A C++ source code contains definition for several classes
  source_class_defn = open("tools/envoy_headersplit/code_corpus/class_defn.h", "r").read()
  # almost the same as above, but classes are not enclosed by namespace
  source_class_defn_without_namespace = open(
      "tools/envoy_headersplit/code_corpus/class_defn_without_namespace.h", "r").read()
  # A C++ source code contains method implementations for class_defn.h
  source_class_impl = open("tools/envoy_headersplit/code_corpus/class_impl.cc", "r").read()

  def test_to_filename(self):
    # Test class name with one "mock"
    self.assertEqual(headersplit.to_filename("MockAdminStream"), "admin_stream")

    # Test class name with two "Mock"
    self.assertEqual(headersplit.to_filename("MockClusterMockPrioritySet"),
                     "cluster_mock_priority_set")

    # Test class name with no "Mock"
    self.assertEqual(headersplit.to_filename("TestRetryHostPredicateFactory"),
                     "test_retry_host_predicate_factory")

  def test_get_directives(self):
    includes = """// your first c++ program
// NOLINT(namespace-envoy)
#include <iostream>

// random strings

#include "foo/bar"

"""
    translation_unit_hello_world = TranslationUnit.from_source(
        "tools/envoy_headersplit/code_corpus/hello.h",
        options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    self.assertEqual(headersplit.get_directives(translation_unit_hello_world), includes)

  def test_class_definitions(self):
    idx = Index.create()
    translation_unit_class_defn = idx.parse("tools/envoy_headersplit/code_corpus/class_defn.h",
                                            ["-x", "c++"])
    defns_cursors = headersplit.class_definitions(translation_unit_class_defn.cursor)
    defns_names = [cursor.spelling for cursor in defns_cursors]
    self.assertEqual(defns_names, ["Foo", "Bar", "FooBar", "DeadBeaf"])
    idx = Index.create()
    translation_unit_class_defn = idx.parse(
        "tools/envoy_headersplit/code_corpus/class_defn_without_namespace.h", ["-x", "c++"])
    defns_cursors = headersplit.class_definitions(translation_unit_class_defn.cursor)
    defns_names = [cursor.spelling for cursor in defns_cursors]
    self.assertEqual(defns_names, [])

  def test_class_implementations(self):
    translation_unit_class_impl = TranslationUnit.from_source(
        "tools/envoy_headersplit/code_corpus/class_impl.cc",
        options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    impls_cursors = headersplit.class_implementations(translation_unit_class_impl.cursor)
    impls_names = [cursor.spelling for cursor in impls_cursors]
    self.assertEqual(impls_names, ["getFoo", "val", "DeadBeaf"])

  def test_class_implementations_error(self):
    # LibClang will fail in parse this source file (it's modified from the original
    # test/server/mocks.cc from Envoy repository) if we don't add flag PARSE_SKIP_FUNCTION_BODIES
    # to ignore function bodies.
    impl_translation_unit = TranslationUnit.from_source(
        "tools/envoy_headersplit/code_corpus/fail_mocks.cc")
    impls_cursors = headersplit.class_implementations(impl_translation_unit.cursor)
    # impls_name is not complete in this case
    impls_names = [cursor.spelling for cursor in impls_cursors]
    # LibClang will stop parsing at
    # MockListenerComponentFactory::MockListenerComponentFactory()
    #     : socket_(std::make_shared<NiceMock<Network::MockListenSocket>>()) {
    #       ^
    # Since parsing stops early, we will have incomplete method list.
    # The reason is not clear, however, this issue can be addressed by adding parsing flag to
    # ignore function body

    # get correct list of member methods
    impl_translation_unit_correct = TranslationUnit.from_source(
        "tools/envoy_headersplit/code_corpus/fail_mocks.cc",
        options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    impls_cursors_correct = headersplit.class_implementations(impl_translation_unit_correct.cursor)
    impls_names_correct = [cursor.spelling for cursor in impls_cursors_correct]
    self.assertNotEqual(impls_names, impls_names_correct)


if __name__ == "__main__":
  unittest.main()
