#!/usr/bin/env python3

#
# This tool allows you to test a tarball of built packages (eg debs, rpms)
# against a configurable set of OS distributions.
#
# usage
#
# with bazel:
#
#  bazel run //tools/distribution:verify -- -h
#
# alternatively, if you have the necessary python deps available
#
#  PYTHONPATH=. ./tools/distribution/verify.py -h
#
# python requires: aiodocker, coloredlogs, frozendict, verboselogs
#

import argparse
import pathlib
import sys
from functools import cached_property
from typing import Optional, Type

import aiodocker

from tools.base import checker, utils
from tools.distribution import distrotest

# TODO(phlax): make this configurable
ENVOY_MAINTAINER = "Envoy maintainers <envoy-maintainers@googlegroups.com>"
ENVOY_VERSION = "1.20.0"


class PackagesConfigurationError(Exception):
    pass


class PackagesDistroChecker(checker.AsyncChecker):
    _active_distrotest = None
    checks = ("distros",)

    @property
    def active_distrotest(self) -> Optional[distrotest.DistroTest]:
        """Currently active test"""
        return self._active_distrotest

    @cached_property
    def config(self) -> dict:
        """Config parsed from the provided path

        Expects a yaml file with distributions in the following format:

        ```yaml

        debian_buster:
          # Docker image tag name
          image: debian:buster-slim
          # File extension of installable packages, for packages signed for
          # particular distributions, this can be the distribution name and `.changes`
          # extension.
          ext: buster.changes

        ubuntu_foo:
          image: ubuntu:foo
          ext: foo.changes

        redhat_8.1:
          image: registry.access.redhat.com/ubi8/ubi:8.1
        ```
        """
        config = utils.from_yaml(self.args.config)
        if not isinstance(config, dict):
            raise PackagesConfigurationError(f"Unable to parse configuration {self.args.config}")
        return config

    @cached_property
    def docker(self) -> aiodocker.Docker:
        """An instance of `aiodocker.Docker`"""
        return aiodocker.Docker()

    @property
    def filter_distributions(self) -> list:
        """List of distributions to filter the tests to be run with"""
        return self.args.distribution

    @property
    def keyfile(self) -> pathlib.Path:
        """Path to a keyfile to to include in the Docker images for verifying
        package signatures
        """
        return pathlib.Path(self.args.keyfile)

    @property
    def packages_tarball(self) -> pathlib.Path:
        """Path to the packages tarball"""
        return pathlib.Path(self.args.packages)

    @property
    def path(self) -> pathlib.Path:
        """Path to a temporary directory to run the tests from"""
        return pathlib.Path(self.tempdir.name)

    @property
    def rebuild(self) -> bool:
        """Flag to rebuild the test images even if they exist"""
        return self.args.rebuild

    @property
    def test_class(self) -> Type[distrotest.DistroTest]:
        """The test class to run the tests with"""
        return distrotest.DistroTest

    @cached_property
    def test_config(self) -> distrotest.DistroTestConfig:
        """The test config

        Parses global and provided configs to store and resolve configurations
        for the test runner.

        Also extracts the packages to the temporary directory and provides info
        on available packages to test with.
        """
        return self.test_config_class(
            docker=self.docker,
            path=self.path,
            tarball=self.packages_tarball,
            keyfile=self.keyfile,
            testfile=self.testfile,
            maintainer=ENVOY_MAINTAINER,
            version=ENVOY_VERSION)

    @property
    def test_config_class(self) -> Type[distrotest.DistroTestConfig]:
        """The test config class"""
        return distrotest.DistroTestConfig

    @property
    def testfile(self) -> pathlib.Path:
        """Path to a testfile to run inside the test containers"""
        return pathlib.Path(self.args.testfile)

    @cached_property
    def tests(self) -> dict:
        """A dictionary of tests and test configuration, filtered according
        to provided args
        """
        _ret = {}
        for name, config in self.config.items():
            if self.filter_distributions and name not in self.filter_distributions:
                continue
            _ret[name] = self.get_test_config(config["image"])
            _ret[name].update(config)
            _ret[name]["packages"] = self.get_test_packages(_ret[name]["type"], _ret[name]["ext"])
        return _ret

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "testfile",
            help="Path to the test file that will be run inside the distribution containers")
        parser.add_argument(
            "config", help="Path to a YAML configuration with distributions for testing")
        parser.add_argument("packages", help="Path to a tarball containing packages to test")
        parser.add_argument(
            "--keyfile",
            "-k",
            help="Specify the path to a file containing a gpg key for verifying packages.")
        parser.add_argument(
            "--distribution",
            "-d",
            nargs="?",
            help="Specify distribution to test. Can be specified multiple times.")
        parser.add_argument(
            "--rebuild", action="store_true", help="Rebuild test images before running the tests.")

    async def check_distros(self) -> None:
        """Check runner"""
        for name, config in self.tests.items():
            self.log.info(
                f"[{name}] Testing with: "
                f"{','.join(p.name for p in config['packages'])}")
            for i, package in enumerate(config["packages"]):
                await self.run_test(name, config["image"], package, (i == 0 and self.rebuild))

    def get_test_config(self, image: str) -> dict:
        """Get the type/ext config for a given image name"""
        return self.test_config.get_config(image)

    def get_test_packages(self, type: str, ext: str) -> list:
        """Get the packages to test for a given type/ext"""
        return self.test_config.get_packages(type, ext)

    async def on_checks_complete(self) -> int:
        """Cleanup and return the test result"""
        await self._cleanup_test()
        await self._cleanup_docker()
        return await super().on_checks_complete()

    async def run_test(self, name: str, image: str, package: pathlib.Path, rebuild: bool) -> None:
        """Runs a test for each of the packages against a particular distro"""
        if self.exiting:
            return
        self.log.info(f"[{name}] Testing package: {package}")
        self._active_distrotest = self.test_class(
            self, self.test_config, name, image, package, rebuild=rebuild)
        await self._active_distrotest.run()

    async def _cleanup_docker(self) -> None:
        """Close the docker connection"""
        if "docker" in self.__dict__:
            await self.docker.close()
            del self.__dict__["docker"]

    async def _cleanup_test(self) -> None:
        """Cleanup test containers"""
        if self.active_distrotest:
            await self.active_distrotest.cleanup()


def main(*args) -> int:
    return PackagesDistroChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
