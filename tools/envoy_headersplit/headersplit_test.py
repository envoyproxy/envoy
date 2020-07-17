# Lint as: python3
"""Tests for headersplit."""

import unittest
import headersplit
import io
import clang.cindex
from clang.cindex import TranslationUnit, Index, CursorKind
clang.cindex.Config.set_library_path("/opt/llvm/lib")


class HeadersplitTest(unittest.TestCase):
  source_code_hello_world = """
// your first c++ program
#include <iostream>

//random strings

#include "foo/bar"

class test{
  test() {
    std::cout<<"Hello World"<<std::endl;
  }
};
  """

  # A C++ source code contains definition for several classes
  # class_definitions() should return a list of cursors, each pointing to one of those classes

  source_class_defn = """
  #include "envoy/split"

  namespace {

  class Foo{
    
  };

  class Bar{
    Foo getFoo();
  };

  class FooBar : Foo, Bar{

  };

  class DeadBeaf{
    public:
      int val();
      FooBar foobar;
  };
  }
  """

  # almost the same as above, but classes are not enclosed by namespace
  source_class_defn_without_namespace = """
  #include "envoy/split"

  class Foo{
    
  };

  class Bar{
    Foo getFoo();
  };

  class FooBar : Foo, Bar{

  };

  class DeadBeaf{
    public:
      int val();
      FooBar foobar;
  };
  """

  source_class_impl = """
  #include "test_class_defn.h"

  namespace{
    Foo Bar::getFoo(){
      Foo foo;
      return foo;
    }

    int DeadBeaf::val(){
      return 42;
    }

    DeadBeaf::DeadBeaf() = default;
  }
 """

  def test_to_filename(self):
    # Test class name with one "mock"
    self.assertEqual(headersplit.to_filename("MockAdminStream"), "admin_stream")

    # Test class name with two "Mock"
    self.assertEqual(headersplit.to_filename("MockClusterMockPrioritySet"),
                     "cluster_mock_priority_set")

    # Test class name with no "Mock"
    self.assertEqual(headersplit.to_filename("TestRetryHostPredicateFactory"),
                     "test_retry_host_predicate_factory")

  def test_get_headers(self):
    includes = """
// your first c++ program
#include <iostream>

//random strings

#include "foo/bar"

"""
    with open("hello.h", "w") as source:
      source.write(self.source_code_hello_world)

    translation_unit_hello_world = TranslationUnit.from_source(
        "hello.h", options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    self.assertEqual(headersplit.get_headers(translation_unit_hello_world), includes)

  def test_class_definitions(self):
    with open("test_class_defn.h", "w") as source:
      source.write(self.source_class_defn)

    idx = Index.create()
    translation_unit_class_defn = idx.parse("test_class_defn.h", ['-x', 'c++'])
    defns_cursors = headersplit.class_definitions(translation_unit_class_defn.cursor)
    defns_names = [cursor.spelling for cursor in defns_cursors]

    self.assertEqual(defns_names, ['Foo', 'Bar', 'FooBar', 'DeadBeaf'])
    with open("test_class_defn_without_namespace.h", "w") as source:
      source.write(self.source_class_defn_without_namespace)

    idx = Index.create()
    translation_unit_class_defn = idx.parse("test_class_defn_without_namespace.h", ['-x', 'c++'])
    defns_cursors = headersplit.class_definitions(translation_unit_class_defn.cursor)
    defns_names = [cursor.spelling for cursor in defns_cursors]

    # class_definitions() should ignore all classes outside the namespaces
    self.assertEqual(defns_names, [])

  def test_class_implementations(self):
    with open("test_class_impl.cc", "w") as source:
      source.write(self.source_class_impl)

    translation_unit_class_impl = TranslationUnit.from_source(
        "test_class_impl.cc", options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    impls_cursors = headersplit.class_implementations(translation_unit_class_impl.cursor)
    impls_names = [cursor.spelling for cursor in impls_cursors]

    self.assertEqual(impls_names, ['getFoo', 'val', 'DeadBeaf'])


if __name__ == '__main__':
  unittest.main()
