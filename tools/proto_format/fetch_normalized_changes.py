#!/usr/bin/env python3

#
# Compare hashes for the local filesystem and the normalized API, any files.
#
# Return a tarball containing normalized versions of any changed or mismatched
# files.
#

import argparse
import io
import pathlib
import sys
import tarfile


def sync(outfile, formatted, local_hash_path, local_change_path, format_hash_path):
    local_hashes = {}
    for line in local_hash_path.read_text().splitlines():
        parts = line.split()
        if len(parts) == 1:
            # Untracked file
            continue
        mode, fhash, __, path = parts
        local_hashes[path] = dict(mode=mode, fhash=fhash)

    format_hashes = {}
    for line in format_hash_path.read_text().splitlines():
        mode, fhash, __, path = line.split()
        format_hashes[path] = dict(mode=mode, fhash=fhash)

    local_changes = []
    for line in local_change_path.read_text().splitlines():
        change, path = line.split()
        local_changes.append(path)

    to_check = []
    to_remove = []
    for path, data in format_hashes.items():
        should_check_update = (
            path in local_changes or path not in local_hashes or data != local_hashes[path])
        if should_check_update:
            to_check.append(path)

    for path in list(local_hashes) + local_changes:
        if path not in format_hashes:
            to_remove.append(path)

    if not to_check and not to_remove:
        with open(outfile, "w") as f:
            f.write("")
        sys.exit(0)

    # Tarballs are only opened if there are updates to check
    with tarfile.open(outfile, "w") as desttar:
        with tarfile.open(formatted) as srctar:
            for update in to_check:
                member = srctar.getmember(f".{update[3:]}")
                desttar.addfile(member, srctar.extractfile(member.name))
            if to_remove:
                data = "\n".join(to_remove).encode("utf-8")
                tarinfo = tarfile.TarInfo("REMOVE")
                tarinfo.size = len(data)
                desttar.addfile(tarinfo, io.BytesIO(data))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('outfile')
    parser.add_argument('--formatted')
    parser.add_argument('--local_hashes')
    parser.add_argument('--local_changes')
    parser.add_argument('--format_hashes')
    args = parser.parse_args()
    sync(
        str(pathlib.Path(args.outfile).absolute()), str(pathlib.Path(args.formatted).absolute()),
        pathlib.Path(args.local_hashes).absolute(),
        pathlib.Path(args.local_changes).absolute(),
        pathlib.Path(args.format_hashes).absolute())
