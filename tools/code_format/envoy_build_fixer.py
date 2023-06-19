#!/usr/bin/env python3

# Enforces:
# - License headers on Envoy BUILD files
# - envoy_package() or envoy_extension_package() top-level invocation for standard Envoy package setup.
# - Infers API dependencies from source files.
# - Misc. cleanups: avoids redundant blank lines, removes unused loads.
# - Maybe more later?

import functools
import os
import re
import subprocess
import sys
import tempfile
import pathlib
import paths

# Where does Buildozer live?
BUILDOZER_PATH = paths.get_buildozer()

# Where does Buildifier live?
BUILDIFIER_PATH = paths.get_buildifier()

# Canonical Envoy license.
LICENSE_STRING = 'licenses(["notice"])  # Apache 2\n\n'

# Match any existing licenses in a BUILD file.
OLD_LICENSES_REGEX = re.compile(r'^licenses\(.*\n+', re.MULTILINE)

# Match an Envoy rule, e.g. envoy_cc_library( in a BUILD file.
ENVOY_RULE_REGEX = re.compile(r'envoy[_\w]+\(')

# Match a load() statement for the envoy_package macros.
PACKAGE_LOAD_BLOCK_REGEX = re.compile('("envoy_package".*?\)\n)', re.DOTALL)
EXTENSION_PACKAGE_LOAD_BLOCK_REGEX = re.compile('("envoy_extension_package".*?\)\n)', re.DOTALL)
CONTRIB_PACKAGE_LOAD_BLOCK_REGEX = re.compile('("envoy_contrib_package".*?\)\n)', re.DOTALL)
MOBILE_PACKAGE_LOAD_BLOCK_REGEX = re.compile('("envoy_mobile_package".*?\)\n)', re.DOTALL)

# Match Buildozer 'print' output. Example of Buildozer print output:
# cc_library json_transcoder_filter_lib [json_transcoder_filter.cc] (missing) (missing)
BUILDOZER_PRINT_REGEX = re.compile(
    '\s*([\w_]+)\s+([\w_]+)\s+[(\[](.*?)[)\]]\s+[(\[](.*?)[)\]]\s+[(\[](.*?)[)\]]')

# Match API header include in Envoy source file?
API_INCLUDE_REGEX = re.compile('#include "(contrib/envoy/.*|envoy/.*)/[^/]+\.pb\.(validate\.)?h"')


class EnvoyBuildFixerError(Exception):
    pass


# Run Buildozer commands on a string representing a BUILD file.
def run_buildozer(cmds, contents):
    with tempfile.NamedTemporaryFile(mode='w') as cmd_file:
        # We send the BUILD contents to buildozer on stdin and receive the
        # transformed BUILD on stdout. The commands are provided in a file.
        cmd_input = '\n'.join('%s|-:%s' % (cmd, target) for cmd, target in cmds)
        cmd_file.write(cmd_input)
        cmd_file.flush()
        r = subprocess.run([BUILDOZER_PATH, '-stdout', '-f', cmd_file.name],
                           input=contents.encode(),
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
        # Buildozer uses 3 for success but no change (0 is success and changed).
        if r.returncode != 0 and r.returncode != 3:
            raise EnvoyBuildFixerError('buildozer execution failed: %s' % r)
        # Sometimes buildozer feels like returning nothing when the transform is a
        # nop.
        if not r.stdout:
            return contents
        return r.stdout.decode('utf-8')


# Add an Apache 2 license and envoy_package() import and rule as needed.
def fix_package_and_license(path, contents):
    regex_to_use = PACKAGE_LOAD_BLOCK_REGEX
    package_string = 'envoy_package'

    if 'source/extensions' in path:
        regex_to_use = EXTENSION_PACKAGE_LOAD_BLOCK_REGEX
        package_string = 'envoy_extension_package'

    if 'contrib/' in path:
        regex_to_use = CONTRIB_PACKAGE_LOAD_BLOCK_REGEX
        package_string = 'envoy_contrib_package'

    if 'mobile/' in path:
        regex_to_use = MOBILE_PACKAGE_LOAD_BLOCK_REGEX
        package_string = 'envoy_mobile_package'

    # Ensure we have an envoy_package import load if this is a real Envoy package. We also allow
    # the prefix to be overridden if envoy is included in a larger workspace.
    if re.search(ENVOY_RULE_REGEX, contents):
        new_load = 'new_load {}//bazel:envoy_build_system.bzl %s' % package_string
        contents = run_buildozer([
            (new_load.format(os.getenv("ENVOY_BAZEL_PREFIX", "")), '__pkg__'),
        ], contents)
        # Envoy package is inserted after the load block containing the
        # envoy_package import.
        package_and_parens = package_string + '()'
        if package_and_parens[:-1] not in contents:
            contents = re.sub(regex_to_use, r'\1\n%s\n\n' % package_and_parens, contents)
            if package_and_parens not in contents:
                raise EnvoyBuildFixerError('Unable to insert %s' % package_and_parens)

    # Delete old licenses.
    if re.search(OLD_LICENSES_REGEX, contents):
        contents = re.sub(OLD_LICENSES_REGEX, '', contents)
    # Add canonical Apache 2 license.
    contents = LICENSE_STRING + contents
    return contents


# Run Buildifier commands on a string with lint mode.
def buildifier_lint(contents):
    r = subprocess.run([BUILDIFIER_PATH, '-lint=fix', '-mode=fix', '-type=build'],
                       input=contents.encode(),
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    if r.returncode != 0:
        raise EnvoyBuildFixerError('buildifier execution failed: %s' % r)
    return r.stdout.decode('utf-8')


# Find all the API headers in a C++ source file.
def find_api_headers(source_path):
    api_hdrs = set([])
    contents = pathlib.Path(source_path).read_text(encoding='utf8')
    for line in contents.split('\n'):
        match = re.match(API_INCLUDE_REGEX, line)
        if match:
            api_hdrs.add(match.group(1))
    return api_hdrs


# Infer and adjust rule dependencies in BUILD files for @envoy_api proto
# files. This is very cheap to do purely via a grep+buildozer syntax level
# step.
#
# This could actually be done much more generally, for all symbols and headers
# if we made use of Clang libtooling semantic analysis. However, this requires a
# compilation database and full build of Envoy, envoy_build_fixer.py is run
# under check_format, which should be fast for developers.
def fix_api_deps(path, contents):
    source_dirname = os.path.dirname(path)
    buildozer_out = run_buildozer([
        ('print kind name srcs hdrs deps', '*'),
    ], contents).strip()
    deps_mutation_cmds = []
    for line in buildozer_out.split('\n'):
        match = re.match(BUILDOZER_PRINT_REGEX, line)
        if not match:
            # buildozer might emit complex multiline output when a 'select' or other
            # macro is used. We're not smart enough to handle these today and they
            # require manual fixup.
            # TODO(htuch): investigate using --output_proto on buildozer to be able to
            # consume something more usable in this situation.
            continue
        kind, name, srcs, hdrs, deps = match.groups()
        if not name:
            continue
        if kind == "envoy_pch_library":
            continue
        source_paths = []
        if srcs != 'missing':
            source_paths.extend(
                os.path.join(source_dirname, f)
                for f in srcs.split()
                if f.endswith('.cc') or f.endswith('.h'))
        if hdrs != 'missing':
            source_paths.extend(
                os.path.join(source_dirname, f) for f in hdrs.split() if f.endswith('.h'))
        api_hdrs = set([])
        for p in source_paths:
            # We're not smart enough to infer on generated files.
            if os.path.exists(p):
                api_hdrs = api_hdrs.union(find_api_headers(p))
        actual_api_deps = set(['@envoy_api//%s:pkg_cc_proto' % h for h in api_hdrs])
        existing_api_deps = set([])
        if deps != 'missing':
            existing_api_deps = set([
                d for d in deps.split()
                if d.startswith('@envoy_api') and d.endswith('pkg_cc_proto')
                and d != '@com_github_cncf_udpa//udpa/annotations:pkg_cc_proto'
            ])
        deps_to_remove = existing_api_deps.difference(actual_api_deps)
        if deps_to_remove:
            deps_mutation_cmds.append(('remove deps %s' % ' '.join(deps_to_remove), name))
        deps_to_add = actual_api_deps.difference(existing_api_deps)
        if deps_to_add:
            deps_mutation_cmds.append(('add deps %s' % ' '.join(deps_to_add), name))
    return run_buildozer(deps_mutation_cmds, contents)


def fix_build(path):
    with open(path, 'r') as f:
        contents = f.read()
    xforms = [
        functools.partial(fix_package_and_license, path),
        functools.partial(fix_api_deps, path),
        buildifier_lint,
    ]
    for xform in xforms:
        contents = xform(contents)
    return contents


if __name__ == '__main__':
    if len(sys.argv) == 2:
        sys.stdout.write(fix_build(sys.argv[1]))
        sys.exit(0)
    elif len(sys.argv) == 3:
        reorderd_source = fix_build(sys.argv[1])
        with open(sys.argv[2], 'w') as f:
            f.write(reorderd_source)
        sys.exit(0)
    print('Usage: %s <source file path> [<destination file path>]' % sys.argv[0])
    sys.exit(1)
