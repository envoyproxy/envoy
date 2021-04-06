import os
import os.path
import shutil


def get_buildifier():
    return os.getenv("BUILDIFIER_BIN") or (
        os.path.expandvars("$GOPATH/bin/buildifier")
        if os.getenv("GOPATH") else shutil.which("buildifier"))


def get_buildozer():
    return os.getenv("BUILDOZER_BIN") or (
        os.path.expandvars("$GOPATH/bin/buildozer")
        if os.getenv("GOPATH") else shutil.which("buildozer"))
