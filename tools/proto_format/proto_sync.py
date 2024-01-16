#!/usr/bin/env python3

# Diff or copy normalized artifacts to the source tree.

import argparse
import difflib
import io
import os
import pathlib
import sys
import tarfile
from hashlib import sha1

# TODO(phlax): Move all of this code to `envoy.code.check`


class Git(object):

    # lifted from:
    #   https://github.com/chris3torek/scripts/blob/2808ac68000c62c3db379d73e3b7df292e333a57/githash.py#L67-L86
    def blob_hash(self, stream, size):
        """
        Return (as hash instance) the hash of a blob,
        as read from the given stream.
        """
        hasher = sha1()
        hasher.update(('blob %u\0' % size).encode('ascii'))
        nread = 0
        while True:
            # We read just 64K at a time to be kind to
            # runtime storage requirements.
            data = stream.read(65536)
            if data == b'':
                break
            nread += len(data)
            hasher.update(data)
        if nread != size:
            # TODO(phlax): move this to pytooling asap.
            #     This would not pass type checking `BytesIO` has no `stream.name`
            raise ValueError('%s: expected %u bytes, found %u bytes' % (stream.name, size, nread))
        return hasher.hexdigest()[:10]

    def string_hash(self, text):
        chars = text.encode("utf-8")
        return self.blob_hash(io.BytesIO(chars), len(chars))

    def diff(self, t1, t2, path, mode, remove=False):
        fhash = self.string_hash(t1)
        fromfile = f"a/{path}"
        tofile = f"b/{path}"
        _diff = [f"diff --git {fromfile} {tofile}"]
        if remove:
            _diff += [f"deleted file mode {mode}"]
            tofile = "/dev/null"
            desthash = "0000000000"
        else:
            desthash = self.string_hash(t2)
        _diff += [f"index {fhash}..{desthash}"]
        changes = list(
            difflib.unified_diff(
                t1.splitlines(), t2.splitlines(), fromfile=fromfile, tofile=tofile))
        return (_diff + changes if remove or changes else [])

    def mode(self, path):
        return oct(os.stat(path).st_mode)[2:]


def sync(api_root, changed, mode, is_ci):
    exitcode = 0
    if os.stat(changed).st_size == 0:
        sys.exit(exitcode)

    api = pathlib.Path(api_root)
    envoy_dir = api.parent
    diff = []
    fixed = []
    removed = []
    git = Git()

    with tarfile.open(changed) as tarball:
        if not tarball.getmembers():
            sys.exit(exitcode)
        for member in tarball.getmembers():
            _diff = []
            _remove = []
            _update = None
            if member.name == "REMOVE":
                to_remove = tarball.extractfile(member.name).read().decode("utf-8")
                for path in to_remove.splitlines():
                    if mode != "check":
                        _remove.append(path)
                    target = pathlib.Path(api_root).parent.joinpath(path)
                    _diff += git.diff(
                        target.read_text(),
                        "",
                        path.lstrip('.').lstrip('/'),
                        git.mode(target),
                        remove=True)
            else:
                target = pathlib.Path(api_root).joinpath(member.name)
                _update = tarball.extractfile(member.name).read().decode("utf-8")
                # The diff here will be empty if the file has changed but is correct.
                _diff += git.diff(
                    target.read_text(), _update, f"api/{member.name.lstrip('.').lstrip('/')}",
                    git.mode(target))
            if not _diff:
                continue
            if mode == "check":
                diff += _diff
                continue
            if _update:
                fixed.append(member.name)
                api.joinpath(member.name).write_text(_update)
                continue
            for path in _remove:
                removed.append(path)
                api.parent.joinpath(path).unlink()

    if mode == "check":
        if not diff:
            sys.exit(0)
        sys.stderr.write("Apply the following diff:\n")
        sys.stderr.write("\n")

        sys.stdout.write("\n".join(d.rstrip() for d in diff))
        sys.stdout.write("\n")
        sys.exit(1)

    if fixed:
        sys.stderr.write("Changes made to following files:\n")
        sys.stderr.write("\n  ")
        sys.stderr.write("\n  ".join(f.rstrip() for f in fixed))
        sys.stderr.write("\n")
        sys.stderr.write("\n")
    if removed:
        sys.stderr.write("Files removed:\n")
        sys.stderr.write("\n  ")
        sys.stderr.write("\n  ".join(f.rstrip() for f in removed))
        sys.stderr.write("\n")
        sys.stderr.write("\n")
    sys.exit(1 if fixed or removed else 0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--changed')
    parser.add_argument('--mode', choices=['check', 'fix'])
    parser.add_argument('--api_root', default='./api')
    parser.add_argument('--ci', action="store_true", default=False)
    args = parser.parse_args()

    sync(
        str(pathlib.Path(args.api_root).absolute()), str(pathlib.Path(args.changed).absolute()),
        args.mode, args.ci)
