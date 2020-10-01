# !/usr/bin/env python3
# Lint as: python3
"""
This python script can dividing monolithic mock headers into different mock classes. We need to
remove the over-included header files in generated class codes and resolve dependencies in the
corresponding Bazel files manually.
"""
import argparse
import os
import subprocess
import sys
from typing import Type, List, Tuple, Dict

# libclang imports
import clang.cindex
from clang.cindex import TranslationUnit, Index, CursorKind, Cursor


def to_filename(classname: str) -> str:
  """
  maps mock class name (in C++ codes) to filenames under the Envoy naming convention.
  e.g. map "MockAdminStream" to "admin_stream"

  Args:
      classname: mock class name from source

  Returns:
      corresponding file name
  """
  filename = classname.replace("Mock", "", 1)  # Remove only first "Mock"
  ret = ""
  for index, val in enumerate(filename):
    if val.isupper() and index > 0:
      ret += "_"
    ret += val
  return ret.lower()


def get_directives(translation_unit: Type[TranslationUnit]) -> str:
  """
  "extracts" all header includes statements and other directives from the target source code file

  for instance:
      foo.h:
      #pragma once
      #include "a.h"
      #include "b.h"

      int foo(){
      }
  this function should return
  '#pragma once\n#include "a.h"\n#include "b.h"'

  Args:
      translation_unit: parsing result of target source code by libclang

  Returns:
      A string, contains all includes statements and other preprocessor directives before the
      first non-directive statement.

  Notes:
      clang lib provides API like tranlation_unit.get_includes() to get include directives.
      But we can't use it as it requires presence of the included files to return the full list.
      We choose to return the string instead of list of includes since we will simply copy-paste
      the include statements into generated headers. Return string seems more convenient
  """
  cursor = translation_unit.cursor
  for descendant in cursor.walk_preorder():
    if descendant.location.file is not None and descendant.location.file.name == cursor.displayname:
      filename = descendant.location.file.name
      contents = read_file_contents(filename)
      return contents[:descendant.extent.start.offset]
  return ""


def cursors_in_same_file(cursor: Cursor) -> List[Cursor]:
  """
  get all child cursors which are pointing to the same file as the input cursor

  Args:
    cursor: cursor of parsing result of target source code by libclang

  Returns:
    a list of cursor
  """
  cursors = []
  for descendant in cursor.walk_preorder():
    # We don't want Cursors from files other than the input file,
    # otherwise we get definitions for every file included
    # when clang parsed the input file (i.e. if we don't limit descendant location,
    # it will check definitions from included headers and get class definitions like std::string)
    if descendant.location.file is None:
      continue
    if descendant.location.file.name != cursor.displayname:
      continue
    cursors.append(descendant)
  return cursors


def class_definitions(cursor: Cursor) -> List[Cursor]:
  """
  extracts all class definitions in the file pointed by cursor. (typical mocks.h)

  Args:
      cursor: cursor of parsing result of target source code by libclang

  Returns:
      a list of cursor, each pointing to a class definition.
  """
  cursors = cursors_in_same_file(cursor)
  class_cursors = []
  for descendant in cursors:
    # check if descendant is pointing to a class declaration block.
    if descendant.kind != CursorKind.CLASS_DECL:
      continue
    if not descendant.is_definition():
      continue
    # check if this class is directly enclosed by a namespace.
    if descendant.semantic_parent.kind != CursorKind.NAMESPACE:
      continue
    class_cursors.append(descendant)
  return class_cursors


def class_implementations(cursor: Cursor) -> List[Cursor]:
  """
  extracts all class implementation in the file pointed by cursor. (typical mocks.cc)

  Args:
      cursor: cursor of parsing result of target source code by libclang

  Returns:
      a list of cursor, each pointing to a class implementation.
  """
  cursors = cursors_in_same_file(cursor)
  impl_cursors = []
  for descendant in cursors:
    if descendant.kind == CursorKind.NAMESPACE:
      continue
    # check if descendant is pointing to a class method
    if descendant.semantic_parent is None:
      continue
    if descendant.semantic_parent.kind == CursorKind.CLASS_DECL:
      impl_cursors.append(descendant)
  return impl_cursors


def extract_definition(cursor: Cursor, classnames: List[str]) -> Tuple[str, str, List[str]]:
  """
  extracts class definition source code pointed by the cursor parameter.
  and find dependent mock classes by naming look up.

  Args:
      cursor: libclang cursor pointing to the target mock class definition.
      classnames: all mock class names defined in the definition header that needs to be
          divided, used to parse class dependencies.
  Returns:
      class_name: a string representing the mock class name.
      class_defn: a string contains the whole class definition body.
      deps: a set of string contains all dependent classes for the return class.

  Note:
      It can not detect and resolve forward declaration and cyclic dependency. Need to address
      manually.
  """
  filename = cursor.location.file.name
  contents = read_file_contents(filename)
  class_name = cursor.spelling
  class_defn = contents[cursor.extent.start.offset:cursor.extent.end.offset] + ";"
  # need to know enclosing semantic parents (namespaces)
  # to generate corresponding definitions
  parent_cursor = cursor.semantic_parent
  while parent_cursor.kind == CursorKind.NAMESPACE:
    if parent_cursor.spelling == "":
      break
    class_defn = "namespace {} {{\n".format(parent_cursor.spelling) + class_defn + "\n}\n"
    parent_cursor = parent_cursor.semantic_parent
  # resolve dependency
  # by simple naming look up
  deps = set()
  for classname in classnames:
    if classname in class_defn and classname != class_name:
      deps.add(classname)
  return class_name, class_defn, deps


def get_implline(cursor: Cursor) -> int:
  """
  finds the first line of implementation source code for class method pointed by the cursor
  parameter. 

  Args:
      cursor: libclang cursor pointing to the target mock class definition.

  Returns:
      an integer, the line number of the first line of the corresponding method implementation
      code (zero indexed)

  Note:
      This function return line number only. Because in certain case libclang will fail in parsing
      the method body and stops parsing early (see headersplit_test.test_class_implementations_error
      for details). To address this issue when parsing implementation code, we passed the flag that
      ask clang to ignore function bodies.
      We can not get the function body directly with the same way we used in extract_definition() 
      since clang didn't parse function this time. Though we can't get the correct method extent
      offset from Cursor, we can still get the start line of the corresponding method instead.
      (We can't get the correct line number for the last line due to skipping function bodies)
  """
  return cursor.extent.start.line - 1


def extract_implementations(impl_cursors: List[Cursor], source_code: str) -> Dict[str, str]:
  """
  extracts method function body for each cursor in list impl_cursors from source code
  groups those function bodies with class name to help generating the divided {classname}.cc
  returns a dict maps class name to the concatenation of all its member methods implementations.

  Args:
      impl_cursors: list of cursors, each pointing to a mock class member function implementation.
      source_code: string, the source code for implementations (e.g. mocks.cc)

  Returns:
      classname_to_impl: a dict maps class name to its member methods implementations
  """
  classname_to_impl = dict()
  for i, cursor in enumerate(impl_cursors):
    classname = cursor.semantic_parent.spelling
    # get first line of function body
    implline = get_implline(cursor)
    # get last line of function body
    if i + 1 < len(impl_cursors):
      # i is not the last method, get the start line for the next method
      # as the last line of i
      impl_end = get_implline(impl_cursors[i + 1])
      impl = "".join(source_code[implline:impl_end])
    else:
      # i is the last method, after removing the lines containing close brackets
      # for namespaces, the rest should be the function body
      offset = 0
      while implline + offset < len(source_code):
        if "// namespace" in source_code[implline + offset]:
          break
        offset += 1
      impl = "".join(source_code[implline:implline + offset])
    if classname in classname_to_impl:
      classname_to_impl[classname] += impl + "\n"
    else:
      classname_to_impl[classname] = impl + "\n"
  return classname_to_impl


def get_enclosing_namespace(defn: Cursor) -> Tuple[str, str]:
  """
  retrieves all enclosing namespaces for the class pointed by defn.
  this is necessary to construct the mock class header
  e.g.:
  defn is pointing MockClass in the follow source code:

  namespace Envoy {
  namespace Server {
  class MockClass2 {...}
  namespace Configuration {
  class MockClass {...}
        ^ 
        defn
  }
  }
  }

  this function will return:
  "namespace Envoy {\nnamespace Server {\nnamespace Configuration{\n" and "\n}\n}\n}\n" 

  Args:
      defn: libclang Cursor pointing to a mock class

  Returns:
      namespace_prefix, namespace_suffix: a pair of string, representing the enclosing namespaces
  """
  namespace_prefix = ""
  namespace_suffix = ""
  parent_cursor = defn.semantic_parent
  while parent_cursor.kind == CursorKind.NAMESPACE:
    if parent_cursor.spelling == "":
      break
    namespace_prefix = "namespace {} {{\n".format(parent_cursor.spelling) + namespace_prefix
    namespace_suffix += "\n}"
    parent_cursor = parent_cursor.semantic_parent
  namespace_suffix += "\n"
  return namespace_prefix, namespace_suffix


def read_file_contents(path):
  with open(path, "r") as input_file:
    return input_file.read()


def write_file_contents(class_name, class_defn, class_impl):
  with open("{}.h".format(to_filename(class_name)), "w") as decl_file:
    decl_file.write(class_defn)
  with open("{}.cc".format(to_filename(class_name)), "w") as impl_file:
    impl_file.write(class_impl)
  # generating bazel build file, need to fill dependency manually
  bazel_text = """
envoy_cc_mock(
  name = "{}_mocks",
  srcs = ["{}.cc"],
  hdrs = ["{}.h"],
  deps = [

  ]
)
""".format(to_filename(class_name), to_filename(class_name), to_filename(class_name))
  with open("BUILD", "r+") as bazel_file:
    contents = bazel_file.read()
    if 'name = "{}_mocks"'.format(to_filename(class_name)) not in contents:
      bazel_file.write(bazel_text)


def main(args):
  """
  divides the monolithic mock file into different mock class files.
  """
  decl_filename = args["decl"]
  impl_filename = args["impl"]
  idx = Index.create()
  impl_translation_unit = TranslationUnit.from_source(
      impl_filename, options=TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
  impl_includes = get_directives(impl_translation_unit)
  decl_translation_unit = idx.parse(decl_filename, ["-x", "c++"])
  defns = class_definitions(decl_translation_unit.cursor)
  decl_includes = get_directives(decl_translation_unit)
  impl_cursors = class_implementations(impl_translation_unit.cursor)
  contents = read_file_contents(impl_filename)
  classname_to_impl = extract_implementations(impl_cursors, contents)
  classnames = [cursor.spelling for cursor in defns]
  for defn in defns:
    # writing {class}.h and {classname}.cc
    class_name, class_defn, deps = extract_definition(defn, classnames)
    includes = ""
    for name in deps:
      includes += '#include "{}.h"\n'.format(to_filename(name))
    class_defn = decl_includes + includes + class_defn
    class_impl = ""
    if class_name not in classname_to_impl:
      print("Warning: empty class {}".format(class_name))
    else:
      impl_include = impl_includes.replace(decl_filename, "{}.h".format(to_filename(class_name)))
      # we need to enclose methods with namespaces
      namespace_prefix, namespace_suffix = get_enclosing_namespace(defn)
      class_impl = impl_include + namespace_prefix + \
          classname_to_impl[class_name] + namespace_suffix
    write_file_contents(class_name, class_defn, class_impl)


if __name__ == "__main__":
  PARSER = argparse.ArgumentParser()
  PARSER.add_argument(
      "-d",
      "--decl",
      default="mocks.h",
      help="Path to the monolithic header .h file that needs to be splitted",
  )
  PARSER.add_argument(
      "-i",
      "--impl",
      default="mocks.cc",
      help="Path to the implementation code .cc file that needs to be splitted",
  )
  main(vars(PARSER.parse_args()))
