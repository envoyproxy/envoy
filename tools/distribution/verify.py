import argparse
import os
import sys
import tarfile
import tempfile
from functools import cached_property
from typing import Optional, Type

import yaml

import aiodocker

from tools.base import checker
from tools.distribution import distrotest


class PackagesDistroChecker(checker.AsyncChecker):
    _active_test = None
    checks = ("distros",)
    _test_types = ()

    @classmethod
    def register_test(cls, name: str, util: Type[distrotest.DistroTest]) -> None:
        """Register util for signing a package type"""
        cls._test_types = getattr(cls, "_test_types") + ((name, util),)

    @property
    def active_test(self) -> Optional[distrotest.DistroTest]:
        return self._active_test

    @cached_property
    def config(self) -> str:
        """Config parsed from the provided path

        Expects a yaml file with distributions in the following format:

        ```yaml

        debian_buster:
          distro: debian
          tag: buster-slim

        ubuntu_foo:
          distro: ubuntu
          tag: foo
        ```

        Adds additional test configuration for the available packages.
        """
        with open(self.args.config) as f:
            return yaml.safe_load(f.read())

    @property
    def distro_test_class(self) -> Type[distrotest.DistroTest]:
        return distrotest.DistroTest

    @cached_property
    def docker(self) -> aiodocker.Docker:
        return aiodocker.Docker()

    @cached_property
    def packages_dir(self) -> str:
        packages_dir = os.path.join(self.path, "packages")
        with tarfile.open(self.packages_tarball) as tar:
            tar.extractall(path=packages_dir)
        return packages_dir

    @property
    def packages_tarball(self) -> str:
        return self.args.packages

    @property
    def path(self) -> str:
        return self.tempdir.name

    @cached_property
    def tempdir(self) -> tempfile.TemporaryDirectory:
        """A temporary directory with a package set and a distribution set extracted"""
        return tempfile.TemporaryDirectory()

    @property
    def test_distributions(self) -> str:
        return self.args.distribution

    @cached_property
    def test_types(self) -> dict:
        return dict(self._test_types)

    @property
    def testfile(self) -> str:
        return self.args.testfile

    @cached_property
    def tests(self):
        _ret = {}
        for distro, config in self.config.items():
            if self.test_distributions and distro not in self.test_distributions:
                continue
            _ret[distro] = config
            config["test_type"] = self._test_type(distro)
            config["packages"] = self._test_packages(config["test_type"])
        return _ret

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "testfile",
            help="Path to the test file that will be run inside the distribution containers")
        parser.add_argument(
            "config",
            help="Path to a YAML configuration with distributions for testing")
        parser.add_argument("packages", help="Path to a tarball containing packages to test")
        parser.add_argument(
            "--distribution",
            "-d",
            nargs="?",
            help="Specify distribution to test. Can be specified multiple times.")

    async def check_distros(self) -> None:
        """Check runner"""

        for distro, config in self.tests.items():
            self.log.info(f"[{distro}] Testing with: " f"{','.join(config['packages'])}")
            for package in config["packages"]:
                await self.distro_test(distro, package, config)

    async def distro_test(self, distro: str, package: str, config: dict) -> tuple:
        """Runs a test for each of the packages against a particular distro"""
        if self.exiting:
            return
        self.log.info(f"[{distro}] Testing package: {package}")
        self._active_test = self.test_types[config["test_type"]](
            self, self.path, package, distro, config["image"], config["tag"])
        errors = await self._active_test.run()
        if errors:
            self.error("distros", errors)

    async def on_checks_complete(self) -> int:
        await self._cleanup_test()
        await self._cleanup_docker()
        await self._cleanup_tempdir()
        return await super().on_checks_complete()

    async def _cleanup_docker(self) -> None:
        if "docker" in self.__dict__:
            await self.docker.close()
            del self.__dict__["docker"]

    async def _cleanup_tempdir(self) -> None:
        if "tempdir" in self.__dict__:
            self.tempdir.cleanup()
            del self.__dict__["tempdir"]

    async def _cleanup_test(self) -> None:
        if self.active_test:
            await self.active_test.cleanup()

    def _pkg_path(self, pkg_type: str, pkg: Optional[str] = None) -> str:
        args = [self.packages_dir, pkg_type]
        if pkg:
            args.append(pkg)
        return os.path.join(*args)

    def _test_packages(self, pkg_type: str) -> list:
        return [
            self._pkg_path(pkg_type, pkg)
            for pkg in os.listdir(self._pkg_path(pkg_type))
            if pkg.endswith(f".{pkg_type}")]

    def _test_type(self, distro: str) -> str:
        # this is a bit brittle - perhaps set it as configuration in bazel
        return "deb" if distro.startswith("debian") or distro.startswith("ubuntu") else "rpm"


def _register_tests() -> None:
    PackagesDistroChecker.register_test("deb", distrotest.DebDistroTest)
    PackagesDistroChecker.register_test("rpm", distrotest.RPMDistroTest)


def main(*args) -> int:
    _register_tests()
    return PackagesDistroChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
