#!/usr/bin/env python3

# You will need to have the respective system tools required for
# package signing to use this tool.
#
# For example you will need debsign to sign debs, and rpmsign to
# sign rpms.
#
# usage
#
# with bazel:
#
#  bazel run //tools/distribution:sign -- -h
#
# alternatively, if you have the necessary python deps available
#
#  PYTHONPATH=. ./tools/distribution/sign.py -h
#
# python requires: coloredlogs, frozendict, python-gnupg, verboselogs
#

import argparse
import pathlib
import shutil
import subprocess
import sys
import tarfile
from functools import cached_property
from itertools import chain
from typing import Iterator, Optional, Tuple, Type, Union

import verboselogs  # type:ignore

from tools.base import runner, utils
from tools.gpg import identity

# Replacable `__` maintainer/gpg config - python interpolation doesnt work easily
# with this string
RPMMACRO_TEMPLATE = """
%_signature gpg
%_gpg_path __GPG_CONFIG__
%_gpg_name __MAINTAINER__
%_gpgbin __GPG_BIN__
%__gpg_sign_cmd %{__gpg} gpg --force-v3-sigs --batch --verbose --no-armor --no-secmem-warning -u "%{_gpg_name}" -sbo %{__signature_filename} --digest-algo sha256 %{__plaintext_filename}'
"""


class SigningError(Exception):
    pass


# Base directory signing util


class DirectorySigningUtil(object):
    """Base class for signing utils - eg for deb or rpm packages"""

    command_name = ""
    _package_type = ""
    ext = ""

    def __init__(
            self,
            path: Union[pathlib.Path, str],
            maintainer: identity.GPGIdentity,
            log: verboselogs.VerboseLogger,
            command: Optional[str] = ""):
        self._path = path
        self.maintainer = maintainer
        self.log = log
        self._command = command

    @cached_property
    def command(self) -> str:
        """Provided command name/path or path to available system version"""
        command = self._command or shutil.which(self.command_name)
        if command:
            return command
        raise SigningError(f"Signing software missing ({self.package_type}): {self.command_name}")

    @property
    def command_args(self) -> tuple:
        return ()

    @property
    def package_type(self) -> str:
        return self._package_type or self.ext

    @property
    def path(self) -> pathlib.Path:
        return pathlib.Path(self._path)

    @property
    def pkg_files(self) -> Tuple[pathlib.Path, ...]:
        """Tuple of paths to package files to sign"""
        # TODO?(phlax): check maintainer/packager field matches key id
        return tuple(
            pkg_file for pkg_file in self.path.glob("*") if pkg_file.name.endswith(f".{self.ext}"))

    def sign(self) -> None:
        """Sign the packages"""
        for pkg in self.pkg_files:
            self.sign_pkg(pkg)

    def sign_command(self, pkg_file: pathlib.Path) -> tuple:
        """Tuple of command parts to sign a specific package"""
        return (self.command,) + self.command_args + (str(pkg_file),)

    def sign_pkg(self, pkg_file: pathlib.Path) -> None:
        """Sign a specific package file"""
        self.log.notice(f"Sign package ({self.package_type}): {pkg_file.name}")
        response = subprocess.run(
            self.sign_command(pkg_file), capture_output=True, encoding="utf-8")

        if response.returncode:
            raise SigningError(response.stdout + response.stderr)

        self.log.success(f"Signed package ({self.package_type}): {pkg_file.name}")


# Runner


class PackageSigningRunner(runner.Runner):
    """For a given `package_type` and `path` this will run the relevant signing
    util for the packages they contain.
    """

    _signing_utils = ()

    @classmethod
    def register_util(cls, name: str, util: Type[DirectorySigningUtil]) -> None:
        """Register util for signing a package type"""
        cls._signing_utils = getattr(cls, "_signing_utils") + ((name, util),)

    @property
    def extract(self) -> bool:
        return self.args.extract

    @cached_property
    def maintainer(self) -> identity.GPGIdentity:
        """A representation of the maintainer with GPG capabilities"""
        return self.maintainer_class(self.maintainer_name, self.maintainer_email, self.log)

    @property
    def maintainer_class(self) -> Type[identity.GPGIdentity]:
        return identity.GPGIdentity

    @property
    def maintainer_email(self) -> str:
        """Email of the maintainer if set"""
        return self.args.maintainer_email

    @property
    def maintainer_name(self) -> str:
        """Name of the maintainer if set"""
        return self.args.maintainer_name

    @property
    def package_type(self) -> str:
        """Package type - eg deb/rpm"""
        return self.args.package_type

    @property
    def path(self) -> pathlib.Path:
        """Path to the packages directory"""
        return pathlib.Path(self.args.path)

    @property
    def tar(self) -> str:
        return self.args.tar

    @cached_property
    def signing_utils(self) -> dict:
        """Configured signing utils - eg `DebSigningUtil`, `RPMSigningUtil`"""
        return dict(getattr(self, "_signing_utils"))

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "path", default="", help="Path to the directory containing packages to sign")
        parser.add_argument(
            "--extract",
            action="store_true",
            help=
            "If set, treat the path as a tarball containing directories according to package_type")
        parser.add_argument("--tar", help="Path to save the signed packages as tar file")
        parser.add_argument(
            "--type",
            default="",
            choices=[c for c in self.signing_utils] + [""],
            help="Package type to sign")
        parser.add_argument(
            "--maintainer-name",
            default="",
            help="Maintainer name to match when searching for a GPG key to match with")
        parser.add_argument(
            "--maintainer-email",
            default="",
            help="Maintainer email to match when searching for a GPG key to match with")

    def archive(self, path: Union[pathlib.Path, str]) -> None:
        with tarfile.open(self.tar, "w") as tar:
            tar.add(path, arcname=".")

    def get_signing_util(self, path: pathlib.Path) -> DirectorySigningUtil:
        return self.signing_utils[path.name](path, self.maintainer, self.log)

    @runner.catches((identity.GPGError, SigningError))
    def run(self) -> None:
        if self.extract:
            self.sign_tarball()
        else:
            self.sign_directory()
        self.log.success("Successfully signed packages")

    def sign(self, path: pathlib.Path) -> None:
        self.log.notice(f"Signing {path.name}s ({self.maintainer}) {str(path)}")
        self.get_signing_util(path).sign()

    def sign_all(self, path: pathlib.Path) -> None:
        for directory in path.glob("*"):
            if directory.name in self.signing_utils:
                self.sign(directory)

    def sign_directory(self) -> None:
        self.sign(self.path)
        if self.tar:
            self.archive(self.path)

    def sign_tarball(self) -> None:
        if not self.tar:
            raise SigningError("You must set a `--tar` file to save to when `--extract` is set")
        with utils.untar(self.path) as tardir:
            self.sign_all(tardir)
            self.archive(tardir)


# RPM


class RPMMacro(object):
    """`.rpmmacros` configuration for rpmsign"""

    _macro_filename = ".rpmmacros"

    def __init__(self, home: Union[pathlib.Path, str], overwrite: bool = False, **kwargs):
        self._home = home
        self.overwrite = bool(overwrite)
        self.kwargs = kwargs

    @property
    def home(self) -> pathlib.Path:
        return pathlib.Path(self._home)

    @property
    def path(self) -> pathlib.Path:
        return self.home.joinpath(self._macro_filename)

    @property
    def macro(self) -> str:
        macro = self.template
        for k, v in self.kwargs.items():
            macro = macro.replace(f"__{k.upper()}__", str(v))
        return macro

    @property
    def template(self) -> str:
        return RPMMACRO_TEMPLATE

    def write(self) -> None:
        if not self.overwrite and self.path.exists():
            return
        self.path.write_text(self.macro)


class RPMSigningUtil(DirectorySigningUtil):
    """Sign all RPM packages in a given directory"""

    command_name = "rpmsign"
    ext = "rpm"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.setup()

    @cached_property
    def command(self) -> str:
        if not (self.maintainer.gpg_bin and self.maintainer.gpg_bin.name == "gpg2"):
            raise SigningError("GPG2 is required to sign RPM packages")
        return super().command

    @cached_property
    def command_args(self) -> tuple:
        return ("--key-id", self.maintainer.fingerprint, "--addsign")

    @property
    def rpmmacro(self) -> Type[RPMMacro]:
        return RPMMacro

    def setup(self) -> None:
        """Create the .rpmmacros file if it doesn't exist"""
        self.rpmmacro(
            self.maintainer.home,
            maintainer=self.maintainer.name,
            gpg_bin=self.maintainer.gpg_bin,
            gpg_config=self.maintainer.gnupg_home).write()

    def sign_pkg(self, pkg_file: pathlib.Path) -> None:
        pkg_file.chmod(0o755)
        super().sign_pkg(pkg_file)


# Deb


class DebChangesFiles(object):
    """Creates a set of `changes` files for specific distros from a src
    `changes` file.

    eg, if src changes file is `envoy_1.100.changes` and `Distribution:`
    field is `buster bullseye`, it creates:

        `envoy_1.100.changes` -> `envoy_1.100.buster.changes`
        `envoy_1.100.changes` -> `envoy_1.100.bullseye.changes`

    while replacing any instances of the original distribution name in
    the respective changes files, eg:

        `buster bullseye` -> `buster`
        `buster bullseye` -> `bullseye`

    finally, it removes the src changes file.
    """

    def __init__(self, src):
        self.src = src

    def __iter__(self) -> Iterator[pathlib.Path]:
        """Iterate the required changes files, creating them, yielding the paths
        of the newly created files, and deleting the original
        """
        for path in self.files:
            yield path
        self.src.unlink()

    @cached_property
    def distributions(self) -> str:
        """Find and parse the `Distributions` header in the `changes` file"""
        with open(self.src) as f:
            line = f.readline()
            while line:
                if not line.startswith("Distribution:"):
                    line = f.readline()
                    continue
                return line.split(":")[1].strip()
        raise SigningError(f"Did not find Distribution field in changes file {self.src}")

    @property
    def files(self) -> Iterator[pathlib.Path]:
        """Create changes files for each distro, yielding the paths"""
        for distro in self.distributions.split():
            yield self.changes_file(distro)

    def changes_file(self, distro: str) -> pathlib.Path:
        """Create a `changes` file for a specific distro"""
        target = self.changes_file_path(distro)
        target.write_text(self.src.read_text().replace(self.distributions, distro))
        return target

    def changes_file_path(self, distro: str) -> pathlib.Path:
        """Path to write the new changes file to"""
        return self.src.with_suffix(f".{distro}.changes")


class DebSigningUtil(DirectorySigningUtil):
    """Sign all `changes` packages in a given directory

    the `.changes` spec allows a single `.changes` file to have multiple `Distributions` listed.

    but, most package repos require a single signed `.change` file per distribution, with only one
    distribution listed.

    this extracts the `.changes` files to -> per-distro `filename.distro.changes`, and removes
    the original, before signing the files.
    """

    command_name = "debsign"
    ext = "changes"
    _package_type = "deb"

    @cached_property
    def command_args(self) -> tuple:
        return ("-k", self.maintainer.fingerprint)

    @property
    def changes_files(self) -> Type[DebChangesFiles]:
        return DebChangesFiles

    @cached_property
    def pkg_files(self) -> tuple:
        """Mangled .changes paths"""
        return tuple(chain.from_iterable(self.changes_files(src) for src in super().pkg_files))


# Setup


def _register_utils() -> None:
    PackageSigningRunner.register_util("deb", DebSigningUtil)
    PackageSigningRunner.register_util("rpm", RPMSigningUtil)


def main(*args) -> int:
    _register_utils()
    return PackageSigningRunner(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
