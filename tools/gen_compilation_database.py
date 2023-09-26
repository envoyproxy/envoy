#!/usr/bin/env python3

import argparse
import json
import os
import shlex
import subprocess
from pathlib import Path


# This method is equivalent to https://github.com/grailbio/bazel-compilation-database/blob/master/generate.py
def generate_compilation_database(args):
    # We need to download all remote outputs for generated source code. This option lives here to override those
    # specified in bazelrc.
    bazel_startup_options = shlex.split(os.environ.get("BAZEL_STARTUP_OPTION_LIST", ""))
    bazel_options = shlex.split(os.environ.get("BAZEL_BUILD_OPTION_LIST", "")) + [
        "--config=compdb",
        "--remote_download_outputs=all",
    ]

    source_dir_targets = args.bazel_targets
    if args.exclude_contrib:
        source_dir_targets.remove("//contrib/...")

    subprocess.check_call([args.bazel, *bazel_startup_options, "build"] + bazel_options + [
        "--aspects=@bazel_compdb//:aspects.bzl%compilation_database_aspect",
        "--output_groups=compdb_files,header_files"
    ] + source_dir_targets)

    execroot = subprocess.check_output([
        args.bazel, *bazel_startup_options, "info", *bazel_options, "execution_root", *bazel_options
    ]).decode().strip()

    db_entries = []
    for db in Path(execroot).glob('**/*.compile_commands.json'):
        db_entries.extend(json.loads(db.read_text()))

    def replace_execroot_marker(db_entry):
        if 'directory' in db_entry and db_entry['directory'] == '__EXEC_ROOT__':
            db_entry['directory'] = execroot
        if 'command' in db_entry:
            db_entry['command'] = (
                db_entry['command'].replace('-isysroot __BAZEL_XCODE_SDKROOT__', ''))
        return db_entry

    return list(map(replace_execroot_marker, db_entries))


def is_header(filename):
    for ext in (".h", ".hh", ".hpp", ".hxx"):
        if filename.endswith(ext):
            return True
    return False


def is_compile_target(target, args):
    filename = target["file"]
    if is_header(filename):
        if args.include_all:
            return True
        if not args.include_headers:
            return False

    if filename.startswith("bazel-out/"):
        if args.include_all:
            return True
        if not args.include_genfiles:
            return False

    if filename.startswith("external/"):
        if args.include_all:
            return True
        if not args.include_external:
            return False

    return True


def modify_compile_command(target, args):
    cc, options = target["command"].split(" ", 1)

    # Workaround for bazel added C++11 options, those doesn't affect build itself but
    # clang-tidy will misinterpret them.
    options = options.replace("-std=c++0x ", "")
    options = options.replace("-std=c++11 ", "")

    if args.vscode:
        # Visual Studio Code doesn't seem to like "-iquote". Replace it with
        # old-style "-I".
        options = options.replace("-iquote ", "-I ")

    if args.system_clang:
        if cc.find("clang"):
            cc = "clang++"

    if is_header(target["file"]):
        options += " -Wno-pragma-once-outside-header -Wno-unused-const-variable"
        options += " -Wno-unused-function"
        # By treating external/envoy* as C++ files we are able to use this script from subrepos that
        # depend on Envoy targets.
        if not target["file"].startswith("external/") or target["file"].startswith(
                "external/envoy"):
            # *.h file is treated as C header by default while our headers files are all C++17.
            options = "-x c++ -std=c++17 -fexceptions " + options

    target["command"] = " ".join([cc, options])
    return target


def fix_compilation_database(args, db):
    db = [modify_compile_command(target, args) for target in db if is_compile_target(target, args)]

    with open("compile_commands.json", "w") as db_file:
        json.dump(db, db_file, indent=2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate JSON compilation database')
    parser.add_argument('--include_external', action='store_true')
    parser.add_argument('--include_genfiles', action='store_true')
    parser.add_argument('--include_headers', action='store_true')
    parser.add_argument('--vscode', action='store_true')
    parser.add_argument('--include_all', action='store_true')
    parser.add_argument('--exclude_contrib', action='store_true')
    parser.add_argument(
        '--system-clang',
        action='store_true',
        help=
        'Use `clang++` instead of the bazel wrapper for commands. This may help if `clangd` cannot find/run the tools.'
    )
    parser.add_argument('--bazel', default='bazel')
    parser.add_argument(
        'bazel_targets', nargs='*', default=[
            "//source/...",
            "//test/...",
            "//contrib/...",
        ])
    args = parser.parse_args()
    fix_compilation_database(args, generate_compilation_database(args))
