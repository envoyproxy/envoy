#!/usr/bin/env python
#
# Script that emits related filenames given an envoy source/include/test filename.
# This can assist a text-editor with a hot-key to rotate between files.  E.g. for
# Emacs this is enabled by loading envoy-emacs-hooks.el.
#
# This script takes a filename, and emits a list of files that are related to it,
# in a deterministic order so that by visiting the first file in the list, you cycle
# through impl, header, test, and interface -- whichever of those happen to exist.

import os
import os.path
import sys

ENVOY_ROOT = "/envoy/"

# We want to search the file from the leaf up for 'envoy', which is the name of the
# top level directory in the git repo.  However, it's also the name of a subdirectory
# of 'include' -- the only subdirectory of 'include' currently, so it's easier just
# to remove it from the input.
fname = sys.argv[1].replace("/include/envoy/", "/include/")

# Parse out the absolute location of this repo, its relative path, and file extension,
# aborting along they way any time there is trouble.
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

def emit(source_path, dest_path, source_ending, dest_ending):
    if fname.endswith(source_ending) and path.startswith(source_path):
        path_len = len(path) - len(source_path) - len(source_ending)
        new_path = absolute_location + dest_path + \
          path[len(source_path):-len(source_ending)] + dest_ending
        if os.path.isfile(new_path):
            print(new_path)

root = path_elements[0]
if root == "test":
    emit("test/common/", "include/envoy/", "_impl_test.cc", ".h")
    emit("test/", "source/", "_test.cc", ".cc")
    emit("test/", "source/", "_test.cc", ".h")
elif root == "source" and ext == ".cc":
    emit("source/", "source/", ".cc", ".h")
    emit("source/", "test/", ".cc", "_test.cc")
    emit("source/common/", "include/envoy/", "_impl.cc", ".h")
elif root == "source" and ext == ".h":
    emit("source/", "test/", ".h", "_test.cc")
    emit("source/common/", "include/envoy/", "_impl.h", ".h")
    emit("source/", "source/", ".h", ".cc")
elif root == "include":
    emit("include/", "source/common/", ".h", "_impl.cc")
    emit("include/", "source/common/", ".h", "_impl.h")
    emit("include/", "test/common/", ".h", "_impl_test.cc")
