# !/usr/bin/env python3
# Lint as: python3
"""
This python script can dividing monolith mock headers
into different mock classes. We need to remove the
over-included head files in generated class codes and
resolve dependencies in the corresponding Bazel files
manually.
"""

from __future__ import print_function

import argparse

import clang.cindex
from clang.cindex import TranslationUnit, Index, CursorKind

clang.cindex.Config.set_library_path("/opt/llvm/lib")


def to_filename(classname):
    """
    maps mock class name (in C++ codes) to filenames under the envoy naming convention.
    e.g. map "MockAdminStream" to "admin_stream"

    Args:
        classname: mock class name from source

    Returns:
        corresponding file name
    """
    filename = classname.replace('Mock', '', 1) # Remove only first "Mock"
    ret = ""
    for i, val in enumerate(filename):
        if val.isupper() and i > 0:
            ret += '_'
        ret += val
    return ret.lower()


def get_headers(translation_unit):
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
    for i in cursor.walk_preorder():
        if i.location.file is not None and i.location.file.name == cursor.displayname:
            filename = i.location.file.name
            with open(filename, 'r') as source_file:
                contents = source_file.read()
            headers = contents[:i.extent.start.offset]
            return headers

    return ""


def class_definitions(cursor):
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


def class_implementations(cursor):
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


def extract_definition(cursor, classnames):
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
    class_defn = contents[cursor.extent.start.offset:cursor.extent.end.
                          offset] + ";"
    # need to know enclosed semantic parents (namespaces)
    # to generate corresponding definitions
    parent_cursor = cursor.semantic_parent
    while parent_cursor.kind == CursorKind.NAMESPACE:
        if parent_cursor.spelling == "":
            break
        class_defn = "namespace {} {{\n".format(
            parent_cursor.spelling) + class_defn + "\n}\n"
        parent_cursor = parent_cursor.semantic_parent

    # resolve dependency
    # by simple naming look up

    deps = set()
    for classname in classnames:
        if classname in class_defn and classname != class_name:
            deps.add(classname)

    return class_name, class_defn, deps


def extract_implementation(cursor):
    """
    extracts class methods implementation source code pointed by the cursor parameter.
    and find dependent mock classes by naming look up.

    Args:
        cursor: libclang cursor pointing to the target mock class definition.

    Returns:
        class_name: a string representing the mock class name.
        implline: the first line of the corresponding impl code

    Note:
        this function return line number only. Because in certain case libclang will fail
        in parsing the method body and return an empty function body instead. So we choose
        not to parse the function body, get the start line and the end line instead.
    """
    class_name = cursor.semantic_parent.spelling
    return class_name, cursor.extent.start.line - 1


def main(args):
    """
    divides the monolith mock file into different mock class files.
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

    classname_to_impl = dict()
    with open(impl_filename, 'r') as source_file:
        contents = source_file.readlines()

    for i, cursor in enumerate(impls):
        classname, implline = extract_implementation(cursor)
        if i + 1 < len(impls):
            _, impl_end = extract_implementation(impls[i + 1])
            impl = ''.join(contents[implline:impl_end])
        else:
            offset = 0
            while implline + offset < len(contents):
                if '// namespace' in contents[implline + offset]:
                    break
                offset += 1
            impl = ''.join(contents[implline:implline + offset])
        try:
            classname_to_impl[classname] += impl + "\n"
        except KeyError:
            classname_to_impl[classname] = impl + "\n"

    classnames = [cursor.spelling for cursor in defns]
    fullclassnames = []
    for cursor in defns:
        classname = cursor.spelling

        parent_cursor = cursor.semantic_parent
        while parent_cursor.kind == CursorKind.NAMESPACE:
            if parent_cursor.spelling == "":
                break
            classname = parent_cursor.spelling + "::" + classname
            parent_cursor = parent_cursor.semantic_parent

        fullclassname = "class " + classname
        fullclassnames.append(fullclassname)
    for defn in defns:
        class_name, class_defn, deps = extract_definition(defn, classnames)
        includes = ""
        for name in deps:
            includes += '#include "{}.h"\n'.format(to_filename(name))
        class_impl = ""
        try:
            impl_include = impl_includes.replace(
                decl_filename, '{}.h'.format(to_filename(class_name)))
            namespace_prefix = ""
            namespace_suffix = ""
            parent_cursor = defn.semantic_parent
            while parent_cursor.kind == CursorKind.NAMESPACE:
                if parent_cursor.spelling == "":
                    break
                namespace_prefix = "namespace {} {{\n".format(
                    parent_cursor.spelling) + namespace_prefix
                namespace_suffix += "\n}\n"
                parent_cursor = parent_cursor.semantic_parent
            class_impl = impl_include + namespace_prefix + \
                classname_to_impl[class_name] + namespace_suffix
        except KeyError:
            print("Warning: empty class {}".format(class_name))
            class_impl = ""
        with open("{}.h".format(to_filename(class_name)), "w") as decl_file:
            decl_file.write(decl_includes + includes + class_defn)
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
""".format(to_filename(class_name), to_filename(class_name),
           to_filename(class_name))
        with open("BUILD", "a") as bazel_file:
            bazel_file.write(bazel_text)


if __name__ == '__main__':
    PARSER = argparse.ArgumentParser()
    PARSER.add_argument(
        '-d',
        '--decl',
        default='mocks.h',
        help="Path to the monolith header .h file that needs to be splitted",
    )
    PARSER.add_argument(
        '-i',
        '--impl',
        default='mocks.cc',
        help="Path to the impl code .cc file that needs to be splitted",
    )
    main(vars(PARSER.parse_args()))
