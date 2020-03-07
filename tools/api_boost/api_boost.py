#!/usr/bin/env python3

# Tool that assists in upgrading the Envoy source tree to the latest API.
# Internally, Envoy uses the latest vN or vNalpha for a given package. Envoy
# will perform a reflection based version upgrade on any older protos that are
# presented to it in configuration at ingestion time.
#
# Usage (from a clean tree):
#
# api_boost.py --generate_compilation_database --build_api_booster

import argparse
import functools
import json
import os
import multiprocessing as mp
import pathlib
import re
import shlex
import subprocess as sp

# Detect API #includes.
API_INCLUDE_REGEX = re.compile('#include "(envoy/.*)/[^/]+\.pb\.(validate\.)?h"')

# Needed for CI to pass down bazel options.
BAZEL_BUILD_OPTIONS = shlex.split(os.environ.get('BAZEL_BUILD_OPTIONS', ''))


# Obtain the directory containing a path prefix, e.g. ./foo/bar.txt is ./foo,
# ./foo/ba is ./foo, ./foo/bar/ is ./foo/bar.
def PrefixDirectory(path_prefix):
  return path_prefix if os.path.isdir(path_prefix) else os.path.dirname(path_prefix)


# Update a C++ file to the latest API.
def ApiBoostFile(llvm_include_path, debug_log, path):
  print('Processing %s' % path)
  if 'API_NO_BOOST_FILE' in pathlib.Path(path).read_text():
    if debug_log:
      print('Not boosting %s due to API_NO_BOOST_FILE\n' % path)
    return None
  # Run the booster
  try:
    result = sp.run([
        './bazel-bin/external/envoy_dev/clang_tools/api_booster/api_booster',
        '--extra-arg-before=-xc++',
        '--extra-arg=-isystem%s' % llvm_include_path, '--extra-arg=-Wno-undefined-internal', path
    ],
                    capture_output=True,
                    check=True)
  except sp.CalledProcessError as e:
    print('api_booster failure for %s: %s %s' % (path, e, e.stderr.decode('utf-8')))
    raise
  if debug_log:
    print(result.stderr.decode('utf-8'))

  # Consume stdout containing the list of inferred API headers.
  return sorted(set(result.stdout.decode('utf-8').splitlines()))


# Rewrite API includes to the inferred headers. Currently this is handled
# outside of the clang-ast-replacements. In theory we could either integrate
# with this or with clang-include-fixer, but it's pretty simply to handle as done
# below, we have more control over special casing as well, so ¯\_(ツ)_/¯.
def RewriteIncludes(args):
  path, api_includes = args
  # Files with API_NO_BOOST_FILE will have None returned by ApiBoostFile.
  if api_includes is None:
    return
  # We just dump the inferred API header includes at the start of the #includes
  # in the file and remove all the present API header includes. This does not
  # match Envoy style; we rely on later invocations of fix_format.sh to take
  # care of this alignment.
  output_lines = []
  include_lines = ['#include "%s"' % f for f in api_includes]
  input_text = pathlib.Path(path).read_text()
  for line in input_text.splitlines():
    if include_lines and line.startswith('#include'):
      output_lines.extend(include_lines)
      include_lines = None
    # Exclude API includes, except for a special case related to v2alpha
    # ext_authz; this is needed to include the service descriptor in the build
    # and is a hack that will go away when we remove v2.
    if re.match(API_INCLUDE_REGEX, line) and 'envoy/service/auth/v2alpha' not in line:
      continue
    output_lines.append(line)
  # Rewrite file.
  pathlib.Path(path).write_text('\n'.join(output_lines) + '\n')


# Update the Envoy source tree the latest API.
def ApiBoostTree(target_paths,
                 generate_compilation_database=False,
                 build_api_booster=False,
                 debug_log=False,
                 sequential=False):
  dep_build_targets = ['//%s/...' % PrefixDirectory(prefix) for prefix in target_paths]

  # Optional setup of state. We need the compilation database and api_booster
  # tool in place before we can start boosting.
  if generate_compilation_database:
    print('Building compilation database for %s' % dep_build_targets)
    sp.run(['./tools/gen_compilation_database.py', '--run_bazel_build', '--include_headers'] +
           dep_build_targets,
           check=True)

  if build_api_booster:
    # Similar to gen_compilation_database.py, we only need the cc_library for
    # setup. The long term fix for this is in
    # https://github.com/bazelbuild/bazel/issues/9578.
    #
    # Figure out some cc_libraries that cover most of our external deps. This is
    # the same logic as in gen_compilation_database.py.
    query = 'kind(cc_library, {})'.format(' union '.join(dep_build_targets))
    dep_lib_build_targets = sp.check_output(['bazel', 'query', query]).decode().splitlines()
    # We also need some misc. stuff such as test binaries for setup of benchmark
    # dep.
    query = 'attr("tags", "compilation_db_dep", {})'.format(' union '.join(dep_build_targets))
    dep_lib_build_targets.extend(sp.check_output(['bazel', 'query', query]).decode().splitlines())
    extra_api_booster_args = []
    if debug_log:
      extra_api_booster_args.append('--copt=-DENABLE_DEBUG_LOG')

    # Slightly easier to debug when we build api_booster on its own.
    sp.run([
        'bazel',
        'build',
        '--strip=always',
        '@envoy_dev//clang_tools/api_booster',
    ] + BAZEL_BUILD_OPTIONS + extra_api_booster_args,
           check=True)
    sp.run([
        'bazel',
        'build',
        '--config=libc++',
        '--strip=always',
    ] + BAZEL_BUILD_OPTIONS + dep_lib_build_targets,
           check=True)

  # Figure out where the LLVM include path is. We need to provide this
  # explicitly as the api_booster is built inside the Bazel cache and doesn't
  # know about this path.
  # TODO(htuch): this is fragile and depends on Clang version, should figure out
  # a cleaner approach.
  llvm_include_path = os.path.join(
      sp.check_output([os.getenv('LLVM_CONFIG'), '--libdir']).decode().rstrip(),
      'clang/9.0.0/include')

  # Determine the files in the target dirs eligible for API boosting, based on
  # known files in the compilation database.
  file_paths = set([])
  for entry in json.loads(pathlib.Path('compile_commands.json').read_text()):
    file_path = entry['file']
    if any(file_path.startswith(prefix) for prefix in target_paths):
      file_paths.add(file_path)
  # Ensure a determinstic ordering if we are going to process sequentially.
  if sequential:
    file_paths = sorted(file_paths)

  # The API boosting is file local, so this is trivially parallelizable, use
  # multiprocessing pool with default worker pool sized to cpu_count(), since
  # this is CPU bound.
  try:
    with mp.Pool(processes=1 if sequential else None) as p:
      # We need multiple phases, to ensure that any dependency on files being modified
      # in one thread on consumed transitive headers on the other thread isn't an
      # issue. This also ensures that we complete all analysis error free before
      # any mutation takes place.
      # TODO(htuch): we should move to run-clang-tidy.py once the headers fixups
      # are Clang-based.
      api_includes = p.map(functools.partial(ApiBoostFile, llvm_include_path, debug_log),
                           file_paths)
      # Apply Clang replacements before header fixups, since the replacements
      # are all relative to the original file.
      for prefix_dir in set(map(PrefixDirectory, target_paths)):
        sp.run(['clang-apply-replacements', prefix_dir], check=True)
      # Fixup headers.
      p.map(RewriteIncludes, zip(file_paths, api_includes))
  finally:
    # Cleanup any stray **/*.clang-replacements.yaml.
    for prefix in target_paths:
      clang_replacements = pathlib.Path(
          PrefixDirectory(prefix)).glob('**/*.clang-replacements.yaml')
      for path in clang_replacements:
        path.unlink()


if __name__ == '__main__':
  parser = argparse.ArgumentParser(description='Update Envoy tree to the latest API')
  parser.add_argument('--generate_compilation_database', action='store_true')
  parser.add_argument('--build_api_booster', action='store_true')
  parser.add_argument('--debug_log', action='store_true')
  parser.add_argument('--sequential', action='store_true')
  parser.add_argument('paths', nargs='*', default=['source', 'test', 'include'])
  args = parser.parse_args()
  ApiBoostTree(args.paths,
               generate_compilation_database=args.generate_compilation_database,
               build_api_booster=args.build_api_booster,
               debug_log=args.debug_log,
               sequential=args.sequential)
