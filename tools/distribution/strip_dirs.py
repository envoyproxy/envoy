
import argparse
import os
import sys
import tarfile
import tempfile

from tools.base import runner


class DirStrippingRunner(runner.Runner):

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument("infile")
        parser.add_argument("outfile")

    def reset(self, tarinfo):
        tarinfo.uname = tarinfo.gname = "envoy"
        return tarinfo

    def run(self):
        with tempfile.TemporaryDirectory() as tempdir:
            with tarfile.open(self.args.infile) as f:
                f.extractall(path=tempdir)

            with tarfile.open(self.args.outfile, "w") as f:
                for root, dirs, files in os.walk(tempdir):
                    for filename in files:
                        fpath = os.path.join(root, filename)
                        f.add(
                            fpath,
                            filter=self.reset,
                            arcname=fpath.replace(tempdir, "").lstrip("/"))


def main(*args) -> int:
    return DirStrippingRunner(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
