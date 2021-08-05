import argparse
import os
import platform
import re
import sys
import tarfile
import tempfile
from functools import cached_property

from colorama import Fore, Style

from sphinx.cmd.build import main as sphinx_build

from tools.base import runner, utils


class SphinxBuildError(Exception):
    pass


class SphinxEnvError(Exception):
    pass


class SphinxRunner(runner.Runner):
    _build_dir = "."
    _build_sha = "UNKNOWN"

    @property
    def blob_sha(self) -> str:
        """Returns either the version tag or the current build sha"""
        return self.docs_tag or self.build_sha

    @property
    def build_dir(self) -> str:
        """Returns current build_dir - most likely a temp directory"""
        return self._build_dir

    @property
    def build_sha(self) -> str:
        """Returns either a provided build_sha or a default"""
        return self.args.build_sha or self._build_sha

    @cached_property
    def colors(self) -> dict:
        """Color scheme for build summary"""
        return dict(chrome=Fore.LIGHTYELLOW_EX, key=Fore.LIGHTCYAN_EX, value=Fore.LIGHTMAGENTA_EX)

    @cached_property
    def config_file(self) -> str:
        """Populates a config file with self.configs and returns the file path"""
        return utils.to_yaml(self.configs, self.config_file_path)

    @property
    def config_file_path(self) -> str:
        """Path to a (temporary) build config"""
        return os.path.join(self.build_dir, "build.yaml")

    @cached_property
    def configs(self) -> str:
        """Build configs derived from provided args"""
        _configs = dict(
            version_string=self.version_string,
            release_level=self.release_level,
            blob_sha=self.blob_sha,
            version_number=self.version_number,
            docker_image_tag_name=self.docker_image_tag_name)
        if self.validator_path:
            _configs["validator_path"] = self.validator_path
        if self.descriptor_path:
            _configs["descriptor_path"] = self.descriptor_path
        return _configs

    @property
    def descriptor_path(self) -> str:
        """Path to a descriptor file for config validation"""
        return os.path.abspath(self.args.descriptor_path)

    @property
    def docker_image_tag_name(self) -> str:
        """Tag name of current docker image"""
        return re.sub(r"([0-9]+\.[0-9]+)\.[0-9]+.*", r"v\1-latest", self.version_number)

    @property
    def docs_tag(self) -> str:
        """Tag name - ie named version for this docs build"""
        return self.args.docs_tag

    @cached_property
    def html_dir(self) -> str:
        """Path to (temporary) directory for outputting html"""
        return os.path.join(self.build_dir, "generated/html")

    @property
    def output_filename(self) -> str:
        """Path to tar file for saving generated html docs"""
        return self.args.output_filename

    @property
    def py_compatible(self) -> bool:
        """Current python version is compatible"""
        return bool(sys.version_info.major == 3 and sys.version_info.minor >= 8)

    @property
    def release_level(self) -> str:
        """Current python version is compatible"""
        return "tagged" if self.docs_tag else "pre-release"

    @cached_property
    def rst_dir(self) -> str:
        """Populates an rst directory with contents of given rst tar,
        and returns the path to the directory
        """
        rst_dir = os.path.join(self.build_dir, "generated/rst")
        if self.rst_tar:
            with tarfile.open(self.rst_tar) as tarfiles:
                tarfiles.extractall(path=rst_dir)
        return rst_dir

    @property
    def rst_tar(self) -> str:
        """Path to the rst tarball"""
        return self.args.rst_tar

    @property
    def sphinx_args(self) -> list:
        """Command args for sphinx"""
        return ["-W", "--keep-going", "--color", "-b", "html", self.rst_dir, self.html_dir]

    @property
    def validator_path(self) -> str:
        """Path to validator utility for validating snippets"""
        return os.path.abspath(self.args.validator_path)

    @property
    def version_file(self) -> str:
        """Path to version files for deriving docs version"""
        return self.args.version_file

    @cached_property
    def version_number(self) -> str:
        """Semantic version"""
        with open(self.version_file) as f:
            return f.read().strip()

    @property
    def version_string(self) -> str:
        """Version string derived from either docs_tag or build_sha"""
        return (
            f"tag-{self.docs_tag}"
            if self.docs_tag else f"{self.version_number}-{self.build_sha[:6]}")

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--build_sha")
        parser.add_argument("--docs_tag")
        parser.add_argument("--version_file")
        parser.add_argument("--validator_path")
        parser.add_argument("--descriptor_path")
        parser.add_argument("rst_tar")
        parser.add_argument("output_filename")

    def build_html(self) -> None:
        if sphinx_build(self.sphinx_args):
            raise SphinxBuildError("BUILD FAILED")

    def build_summary(self) -> None:
        print()
        print(self._color("#### Sphinx build configs #####################"))
        print(self._color("###"))
        for k, v in self.configs.items():
            print(f"{self._color('###')} {self._color(k, 'key')}: {self._color(v, 'value')}")
        print(self._color("###"))
        print(self._color("###############################################"))
        print()

    def check_env(self) -> None:
        if not self.py_compatible:
            raise SphinxEnvError(
                f"ERROR: python version must be >= 3.8, you have {platform.python_version()}")
        if not self.configs["release_level"] == "tagged":
            return
        if f"v{self.version_number}" != self.docs_tag:
            raise SphinxEnvError(
                "Given git tag does not match the VERSION file content:"
                f"{self.docs_tag} vs v{self.version_number}")
        with open(os.path.join(self.rst_dir, "version_history/current.rst")) as f:
            if not self.version_number in f.read():
                raise SphinxEnvError(
                    f"Git tag ({self.version_number}) not found in version_history/current.rst")

    def create_tarball(self) -> None:
        with tarfile.open(self.output_filename, "w") as tar:
            tar.add(self.html_dir, arcname=".")

    def run(self) -> int:
        with tempfile.TemporaryDirectory() as build_dir:
            return self._run(build_dir)

    def _color(self, msg, name=None):
        return f"{self.colors[name or 'chrome']}{msg}{Style.RESET_ALL}"

    def _run(self, build_dir):
        self._build_dir = build_dir
        os.environ["ENVOY_DOCS_BUILD_CONFIG"] = self.config_file
        try:
            self.check_env()
        except SphinxEnvError as e:
            print(e)
            return 1
        self.build_summary()
        try:
            self.build_html()
        except SphinxBuildError as e:
            print(e)
            return 1
        self.create_tarball()


def main(*args) -> int:
    return SphinxRunner(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
