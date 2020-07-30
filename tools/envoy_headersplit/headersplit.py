# !/usr/bin/env python3
# Lint as: python3
"""
This python script can dividing monolithic mock headers
into different mock classes. We need to remove the
over-included head files in generated class codes and
resolve dependencies in the corresponding Bazel files
manually.
"""

from __future__ import print_function

import argparse
from typing import Type, List, Tuple, Dict
import clang.cindex
from clang.cindex import TranslationUnit, Index, CursorKind, Cursor

clang.cindex.Config.set_library_path("/opt/llvm/lib")


def to_filename(classname: str) -> str:
  """
    maps mock class name (in C++ codes) to filenames under the Envoy naming convention.
    e.g. map "MockAdminStream" to "admin_stream"

    Args:
        classname: mock class name from source

    Returns:
        corresponding file name
    """
  filename = classname.replace('Mock', '', 1)  # Remove only first "Mock"
  ret = ""
  for index, val in enumerate(filename):
    if val.isupper() and index > 0:
      ret += '_'
    ret += val
  return ret.lower()


def get_headers(translation_unit: Type[TranslationUnit]) -> str:
  """
    extracts all head includes statements from the target code file (translation_unit)

    for instance:
        foo.h:
        #include "a.h"
        #include "b.h"

        int foo(){

        }
    this function should return
    '#include "a.h"\n#include "b.h"'

    Args:
        translation_unit: parsing result of target source code by libclang

    Returns:
        A string, contains all includes statements from the source code.

    """

  # clang lib provides API like tranlation_unit.get_inludes()
  # But we can't use it since it requires presence of the included files to return the full list

  cursor = translation_unit.cursor
  for descendant in cursor.walk_preorder():
    if descendant.location.file is not None and descendant.location.file.name == cursor.displayname:
      filename = descendant.location.file.name
      with open(filename, 'r') as source_file:
        contents = source_file.read()
      headers = contents[:descendant.extent.start.offset]
      return headers

  return ""


def class_definitions(cursor: Cursor) -> List[Cursor]:
  """
    extracts all class definitions in the file pointed by cursor. (typical mocks.h)

    Args:
        cursor: cursor of parsing result of target souce code by libclang

    Returns:
        a list of cursor, each pointing to a class definition.

    """
  class_cursors = []
  for i in cursor.walk_preorder():
    if i.location.file is None:
      continue
    if i.location.file.name != cursor.displayname:
      continue
    if i.kind != CursorKind.CLASS_DECL:
      continue
    if not i.is_definition():
      continue
    if i.semantic_parent.kind != CursorKind.NAMESPACE:
      continue
    class_cursors.append(i)
  return class_cursors


def class_implementations(cursor: Cursor) -> List[Cursor]:
  """
    extracts all class implementation in the file pointed by cursor. (typical mocks.cc)

    Args:
        cursor: cursor of parsing result of target souce code by libclang

    Returns:
        a list of cursor, each pointing to a class implementation.

    """
  impl_cursors = []
  for i in cursor.walk_preorder():
    if i.location.file is None:
      continue
    if i.location.file.name != cursor.displayname:
      continue
    if i.kind == CursorKind.NAMESPACE:
      continue
    if i.semantic_parent is not None and i.semantic_parent.kind == CursorKind.CLASS_DECL:
      impl_cursors.append(i)
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
        class_defn: a string contatins the whole class definition body.
        deps: a set of string contatins all dependent classes for the return class.

    Note:
        It can not detect and resolve forward declaration and cyclic dependency. Need to address
        manually.
    """
  filename = cursor.location.file.name
  with open(filename, 'r') as source_file:
    contents = source_file.read()
  class_name = cursor.spelling
  class_defn = contents[cursor.extent.start.offset:cursor.extent.end.offset] + ";"
  # need to know enclosed semantic parents (namespaces)
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
    find the first line of implementation source code for class method pointed by the cursor parameter.
    and find dependent mock classes by naming look up.

    Args:
        cursor: libclang cursor pointing to the target mock class definition.

    Returns:
        implline: the first line of the corresponding method implementation code

    Note:
        This function return line number only. Because in certain case libclang will fail
        in parsing the method body and stops parsing early (see headersplit_test.test_class_implementations_error for details).
        To address this issue when parsing implementation code, we passed the flag that ask clang to ignore function bodies.
        We can not get the function body directly with the same way we used in extract_definition() since clang didn't parse 
        function this time. Though we can't get the correct method extent offset from Cursor, we can still
        get the start line and the end line of the corresponding method instead.
    """
  return cursor.extent.start.line - 1


def extract_implementations(impls: List[Cursor], contents: str) -> Dict[str, str]:
  classname_to_impl = dict()
  for i, cursor in enumerate(impls):
    classname = cursor.semantic_parent.spelling
    implline = get_implline(cursor)
    if i + 1 < len(impls):
      _, impl_end = get_implline(impls[i + 1])
      impl = ''.join(contents[implline:impl_end])
    else:
      offset = 0
      while implline + offset < len(contents):
        if '// namespace' in contents[implline + offset]:
          break
        offset += 1
      impl = ''.join(contents[implline:implline + offset])
    if classname in classname_to_impl:
      classname_to_impl[classname] += impl + "\n"
    else:
      classname_to_impl[classname] = impl + "\n"
  return classname_to_impl


def get_fullclassname(cursor: Cursor) -> str:
  classname = cursor.spelling
  parent_cursor = cursor.semantic_parent
  while parent_cursor.kind == CursorKind.NAMESPACE:
    if parent_cursor.spelling == "":
      break
    classname = parent_cursor.spelling + "::" + classname
    parent_cursor = parent_cursor.semantic_parent

  fullclassname = "class " + classname
  return fullclassname


def get_enclosed_namespace(defn):
  namespace_prefix = ""
  namespace_suffix = ""
  parent_cursor = defn.semantic_parent
  while parent_cursor.kind == CursorKind.NAMESPACE:
    if parent_cursor.spelling == "":
      break
    namespace_prefix = "namespace {} {{\n".format(parent_cursor.spelling) + namespace_prefix
    namespace_suffix += "\n}\n"
    parent_cursor = parent_cursor.semantic_parent
  return namespace_prefix, namespace_suffix


def write_file(class_name, class_defn, class_impl):
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
  with open("BUILD", "a") as bazel_file:
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

  impl_includes = get_headers(impl_translation_unit)

  decl_translation_unit = idx.parse(decl_filename, ['-x', 'c++'])
  defns = class_definitions(decl_translation_unit.cursor)
  decl_includes = get_headers(decl_translation_unit)

  impls = class_implementations(impl_translation_unit.cursor)

  with open(impl_filename, 'r') as source_file:
    contents = source_file.readlines()
  classname_to_impl = extract_implementations(impls, contents)

  classnames = [cursor.spelling for cursor in defns]

  fullclassnames = [get_fullclassname(cursor) for cursor in defns]

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
      impl_include = impl_includes.replace(decl_filename, '{}.h'.format(to_filename(class_name)))
      # we need to enclose methods with namespaces
      namespace_prefix, namespace_suffix = get_enclosed_namespace(defn)
      class_impl = impl_include + namespace_prefix + \
          classname_to_impl[class_name] + namespace_suffix

    write_file(class_name, class_defn, class_impl)


if __name__ == '__main__':
  PARSER = argparse.ArgumentParser()
  PARSER.add_argument(
      '-d',
      '--decl',
      default='mocks.h',
      help="Path to the monolithic header .h file that needs to be splitted",
  )
  PARSER.add_argument(
      '-i',
      '--impl',
      default='mocks.cc',
      help="Path to the implementation code .cc file that needs to be splitted",
  )
  main(vars(PARSER.parse_args()))
