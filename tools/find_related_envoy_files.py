#!/usr/bin/env python
#
# Emits related filenames given an envoy source/include/test filename.
# This can assist a text-editor with a hot-key to rotate between
# files. E.g. for Emacs this is enabled by loading
# envoy-emacs-hooks.el.
#
# Takes a filename as its only arg, and emits a list of files that are
# related to it, in a deterministic order so that by visiting the
# first file in the list, you cycle through impl, header, test, and
# interface -- whichever of those happen to exist. One file is emitted
# per line.

import os
import os.path
import sys

# Name of top level git directory.
ENVOY_ROOT = "/envoy/"

SOURCE_ROOT = "source"
TEST_ROOT = "test"
INTERFACE_REAL_ROOT = "include/envoy"

# Synthetic name for /include/envoy/, which helps us disambiguate
# the 'envoy' underneath include from the top level of the git repo.
INTERFACE_SYNTHETIC_ROOT = "include-envoy"

# We want to search the file from the leaf up for 'envoy', which is
# the name of the top level directory in the git repo. However, it's
# also the name of a subdirectory of 'include' -- the only
# subdirectory of 'include' currently, so it's easier just to treat
# that as a single element.
fname = sys.argv[1].replace("/" + INTERFACE_REAL_ROOT + "/",
                            "/" + INTERFACE_SYNTHETIC_ROOT + "/")

# Parse the absolute location of this repo, its relative path, and
# file extension, exiting with no output along the way any time there
# is trouble.
envoy_index = fname.rfind(ENVOY_ROOT)
if envoy_index == -1:
  sys.exit(0)
envoy_index += len(ENVOY_ROOT)
absolute_location = fname[0:envoy_index]  # "/path/to/gitroot/envoy/"
path = fname[envoy_index:]
path_elements = path.split("/")
if len(path_elements) < 3:
  sys.exit(0)
leaf = path_elements[len(path_elements) - 1]
dot = leaf.rfind(".")
if dot == -1 or dot == len(leaf) - 1:
  sys.exit(0)
ext = leaf[dot:]

# Transforms the input filename based on some transformation rules. Nothing
# is emitted if the input path or extension does not match the expected pattern,
# or if the file doesn't exist.
def emit(source_path, dest_path, source_ending, dest_ending):
  if fname.endswith(source_ending) and path.startswith(source_path + "/"):
    path_len = len(path) - len(source_path) - len(source_ending)
    new_path = (absolute_location + dest_path +
                path[len(source_path):-len(source_ending)] + dest_ending)
    if os.path.isfile(new_path):
      print(new_path)

# Depending on which type of file is passed into the script: test, cc,
# h, or interface, emit any related ones in cyclic order.
root = path_elements[0]
if root == TEST_ROOT:
  emit("test/common", INTERFACE_REAL_ROOT, "_impl_test.cc", ".h")
  emit(TEST_ROOT, SOURCE_ROOT, "_test.cc", ".cc")
  emit(TEST_ROOT, SOURCE_ROOT, "_test.cc", ".h")
elif root == SOURCE_ROOT and ext == ".cc":
  emit(SOURCE_ROOT, SOURCE_ROOT, ".cc", ".h")
  emit(SOURCE_ROOT, TEST_ROOT, ".cc", "_test.cc")
  emit("source/common", INTERFACE_REAL_ROOT, "_impl.cc", ".h")
elif root == SOURCE_ROOT and ext == ".h":
  emit(SOURCE_ROOT, TEST_ROOT, ".h", "_test.cc")
  emit("source/common", INTERFACE_REAL_ROOT, "_impl.h", ".h")
  emit(SOURCE_ROOT, SOURCE_ROOT, ".h", ".cc")
elif root == INTERFACE_SYNTHETIC_ROOT:
  emit(INTERFACE_SYNTHETIC_ROOT, "source/common", ".h", "_impl.cc")
  emit(INTERFACE_SYNTHETIC_ROOT, "source/common", ".h", "_impl.h")
  emit(INTERFACE_SYNTHETIC_ROOT, "test/common", ".h", "_impl_test.cc")
