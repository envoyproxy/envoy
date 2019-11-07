#!/usr/bin/env python

# Enforce header order in a given file. This will only reorder in the first sequence of contiguous
# #include statements, so it will not play well with #ifdef.
#
# This attempts to enforce the guidelines at
# https://google.github.io/styleguide/cppguide.html#Names_and_Order_of_Includes
# with some allowances for Envoy-specific idioms.
#
# There is considerable overlap with what this does and clang-format's IncludeCategories (see
# https://clang.llvm.org/docs/ClangFormatStyleOptions.html). But, clang-format doesn't seem smart
# enough to handle block splitting and correctly detecting the main header subject to the Envoy
# canonical paths.

from __future__ import print_function

import argparse
import common
import re
import sys


def ReorderHeaders(path):
  with open(path, 'r') as f:
    source = f.read()

  all_lines = iter(source.split('\n'))
  before_includes_lines = []
  includes_lines = []
  after_includes_lines = []

  # Collect all the lines prior to the first #include in before_includes_lines.
  try:
    while True:
      line = next(all_lines)
      if line.startswith('#include'):
        includes_lines.append(line)
        break
      before_includes_lines.append(line)
  except StopIteration:
    pass

  # Collect all the #include and whitespace lines in includes_lines.
  try:
    while True:
      line = next(all_lines)
      if not line:
        continue
      if not line.startswith('#include'):
        after_includes_lines.append(line)
        break
      includes_lines.append(line)
  except StopIteration:
    pass

  # Collect the remaining lines in after_includes_lines.
  after_includes_lines += list(all_lines)

  # Filter for includes that finds the #include of the header file associated with the source file
  # being processed. E.g. if 'path' is source/common/common/hex.cc, this filter matches
  # "common/common/hex.h".
  def file_header_filter():
    return lambda f: f.endswith('.h"') and path.endswith(f[1:-3] + '.cc')

  def regex_filter(regex):
    return lambda f: re.match(regex, f)

  # Filters that define the #include blocks
  block_filters = [
      file_header_filter(),
      regex_filter('<.*\.h>'),
      regex_filter('<.*>'),
  ]
  for subdir in include_dir_order:
    block_filters.append(regex_filter('"' + subdir + '/.*"'))

  blocks = []
  already_included = set([])
  for b in block_filters:
    block = []
    for line in includes_lines:
      header = line[len('#include '):]
      if line not in already_included and b(header):
        block.append(line)
        already_included.add(line)
    if len(block) > 0:
      blocks.append(block)

  # Anything not covered by block_filters gets its own block.
  misc_headers = list(set(includes_lines).difference(already_included))
  if len(misc_headers) > 0:
    blocks.append(misc_headers)

  reordered_includes_lines = '\n\n'.join(['\n'.join(sorted(block)) for block in blocks])

  if reordered_includes_lines:
    reordered_includes_lines += '\n'

  return '\n'.join(
      filter(lambda x: x, [
          '\n'.join(before_includes_lines),
          reordered_includes_lines,
          '\n'.join(after_includes_lines),
      ]))


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Header reordering.')
  parser.add_argument('--path', type=str, help='specify the path to the header file')
  parser.add_argument('--rewrite', action='store_true', help='rewrite header file in-place')
  parser.add_argument('--include_dir_order',
                      type=str,
                      default=','.join(common.includeDirOrder()),
                      help='specify the header block include directory order')
  args = parser.parse_args()
  target_path = args.path
  include_dir_order = args.include_dir_order.split(',')
  reorderd_source = ReorderHeaders(target_path)
  if args.rewrite:
    with open(target_path, 'w') as f:
      f.write(reorderd_source)
  else:
    sys.stdout.write(reorderd_source)
