import os
import shutil
import sys
import tarfile
import tempfile
from functools import cached_property

import yaml

import aiodocker

from tools.base import checker


class BuildError(Exception):
    pass


class ContainerError(Exception):
    pass


class DistroTest(object):

    def __init__(self, checker, path, installable):
        self.checker = checker
        self.path = path
        self.installable = installable

    @property
    def buildargs(self):
        args = dict(
            DISTRONAME=self.distro,
            BINARY=os.path.basename(self.installable),
            PACKAGE=os.path.basename(self.installable).split("_")[0])
        if not os.path.exists(self.env_file):
            return args
        with open(self.env_file) as f:
            args.update({k: str(v) for k, v in yaml.safe_load(f.read()).items()})
        return args

    @property
    def distro(self):
        return os.path.basename(self.path)

    @property
    def docker(self):
        return self.checker.docker

    @property
    def env_file(self):
        return os.path.join(self.path, "env")

    @property
    def tag(self):
        return self.distro

    async def build_image(self):
        with tempfile.TemporaryDirectory() as tempdir:
            f = tempfile.NamedTemporaryFile()
            return await self._build_image(tempdir, f)

    async def exec(self, container, cmd):
        execute = await container.exec(cmd, stderr=True)

        async with execute.start(detach=False) as stream:
            while True:
                msg = await stream.read_out()
                if not msg:
                    break
                sys.stdout.buffer.write(msg.data)
        return (await execute.inspect())["ExitCode"]

    async def run(self):
        try:
            await self.build_image()
            self.checker.log.info(f"[{self.distro}] Image built")
        except BuildError as e:
            return e.args
        try:
            container = await self.start_container()
        except ContainerError as e:
            return e.args
        results = await self.run_test(container)
        await container.stop()
        self.checker.log.info(f"[{self.distro}] Container stopped")
        return results

    async def run_test(self, container):
        if await self.exec(container, ["./tmp/distrotest.sh", self.installable, self.distro]):
            return [f"[{self.distro}] Checks did not complete successfully"]

    async def start_container(self):
        container = await self.docker.containers.create_or_replace(
            config={'Image': self.distro},
            name='testing')
        await container.start()
        info = await container.show()
        if not info["State"]["Running"]:
            for log in await container.log(stdout=True, stderr=True):
                self.checker.log.error(log)
            raise ContainerError(f"[{self.distro}] Container unable to start")
        print(f"[{self.distro}] Container started")
        return container

    async def _build_image(self, build_dir, f):
        with tarfile.open(os.path.join(build_dir, "build.tar"), fileobj=f, mode="w") as tar:
            tar.add(self.path, arcname=".")
        f.seek(0)
        lines = ""
        try:
            build = await self.docker.images.build(fileobj=f, encoding="gzip", tag=self.tag, buildargs=self.buildargs)
        except aiodocker.exceptions.DockerError as e:
            raise BuildError(e)
        for line in build:
            if "stream" in line:
                lines += line["stream"]
        try:
            await self.docker.images.inspect(self.tag)
        except aiodocker.exceptions.DockerError:
            self.checker.log.error(lines)
            raise BuildError(f"[{self.distro}] Image {self.tag} failed to build")


class DistroChecker(checker.AsyncChecker):
    checks = ("distros", )

    @property
    def build_path(self):
        return self.args.build

    @cached_property
    def deb_paths(self):
        return [
            os.path.join(self.tempdir.name, "deb", deb)
            for deb in os.listdir(os.path.join(self.tempdir.name, "deb"))
            if deb.endswith("deb")]

    @cached_property
    def docker(self):
        return aiodocker.Docker()

    @property
    def distribution_path(self):
        return self.args.distribution

    @cached_property
    def rpm_paths(self):
        return [
            os.path.join(self.tempdir.name, "rpm", rpm)
            for rpm in os.listdir(os.path.join(self.tempdir.name, "rpm"))
            if rpm.endswith("rpm")]

    @cached_property
    def tempdir(self):
        tempdir = tempfile.TemporaryDirectory()

        with tarfile.open(self.distribution_path) as tar:
            # strip leading slash
            for member in tar.getmembers():
                member.name = member.name.lstrip("/")
                tar.extract(member, tempdir.name)

        with tarfile.open(self.build_path) as tar:
            tar.extractall(path=tempdir.name)
        for root, dirs, files in os.walk(tempdir.name):
            for fname in files:
                print(os.path.join(root, fname))
        return tempdir

    @property
    def testfile(self):
        return self.args.testfile

    def add_arguments(self, parser):
        super().add_arguments(parser)
        parser.add_argument("testfile")
        parser.add_argument("build")
        parser.add_argument("distribution")
        parser.add_argument("--tests", nargs="?")

    async def check_distros(self):
        for distro in os.listdir(os.path.join(self.tempdir.name, "distros")):
            if self.args.tests and not distro in self.args.tests:
                continue
            errors = await self.distro_test(distro)
            if errors:
                self.error(f"distro", errors)
            else:
                self.log.info(f"[{distro}] Test completed successfully")

    async def distro_test(self, distro):
        distro_dir = self.get_distro_dir(distro)
        self._add_testfile(distro_dir)
        paths = (
            self.deb_paths
            if distro.startswith("debian") or distro.startswith("ubuntu")
            else self.rpm_paths)
        errors = []
        for installable in paths:
            installable = self._add_binary(distro, distro_dir, installable)
            errors += await DistroTest(self, distro_dir, installable).run() or []
        return errors

    def get_distro_dir(self, distro):
        return os.path.join(self.tempdir.name, "distros", distro)

    async def on_checks_complete(self) -> int:
        await self.docker.close()
        self.tempdir.cleanup()
        return await super().on_checks_complete()

    def _add_testfile(self, distro_dir):
        shutil.copyfile(self.testfile, os.path.join(distro_dir, os.path.basename(self.testfile)))

    def _add_binary(self, distro, distro_dir, installable):
        return shutil.copyfile(installable, os.path.join(distro_dir, os.path.basename(installable)))


def main(*args):
    return DistroChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
