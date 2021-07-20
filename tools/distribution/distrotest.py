import itertools
import logging
import os
import shutil
from functools import cached_property
from typing import Callable, Iterable, Optional, Tuple, Type, Union

import verboselogs

import aiodocker

from tools.base import checker
from tools.docker import utils as docker_utils

DEB_BUILD_COMMAND = (
    "chmod +x /tmp/distrotest.sh "
    "&& apt-get update "
    "&& apt-get install -y -qq --no-install-recommends curl procps sudo")
DEB_ENV = "ENV DEBIAN_FRONTEND=noninteractive"
DEB_INSTALL_COMMAND="apt-get install -yy -q "
DEB_MAINTAINER_COMMAND="apt show"
DEB_UNINSTALL_COMMAND="apt-get remove --purge -y -qq"

DOCKERFILE_TEMPLATE = """
FROM {distro}:{tag}
{env}

ADD {install_dir} {install_mount_path}
ADD {test_filename} {test_mount_path}
RUN {build_command}

CMD ["tail", "-f", "/dev/null"]
"""
# CMD ["echo", "EXITING"]

RPM_BUILD_COMMAND = (
    "chmod +x /tmp/distrotest.sh "
    "&& yum -y install procps sudo")
RPM_INSTALL_COMMAND="rpm -i --force"
RPM_UNINSTALL_COMMAND="rpm -e"
RPM_MAINTAINER_COMMAND="rpm -qi"


class BuildError(Exception):
    pass


class ContainerError(Exception):
    pass


class DistroTestImage(object):
    package_type = None

    def __init__(
            self,
            docker: aiodocker.Docker,
            path: str,
            build_image: str,
            build_tag: str,
            testfile: str,
            distro: str,
            stream: Optional[Callable] = None):
        self.docker = docker
        self.path = path
        self.build_image = build_image
        self.build_tag = build_tag
        self.testfile = testfile
        self.distro = distro
        self.stream = stream

    @property
    def build_command(self) -> str:
        raise NotImplementedError

    @property
    def build_env(self) -> str:
        return ""

    @cached_property
    def dockerfile(self) -> str:
        return self.dockerfile_template.format(
            distro=self.build_image,
            tag=self.build_tag,
            install_dir=self.install_dir,
            env=self.build_env,
            install_mount_path=self.mount_install_dir,
            test_filename=self.testfile_name,
            test_mount_path=self.mount_testfile_path,
            build_command=self.build_command)

    @cached_property
    def dockerfile_path(self):
        return os.path.join(self.path, "Dockerfile")

    @property
    def dockerfile_template(self):
        return DOCKERFILE_TEMPLATE

    @property
    def install_dir(self) -> str:
        return os.path.join("packages", self.package_type)

    @property
    def mount_install_dir(self) -> str:
        return "/tmp/install"

    @property
    def mount_testfile_path(self) -> str:
        """Path to the testfile inside the test container"""
        # as this is a path inside the linux container - dont use `os`
        return f"/tmp/{self.testfile_name}"

    @cached_property
    def tag(self) -> str:
        """Tag for the Docker test image build"""
        return f"{self.distro}:latest"

    @property
    def testfile_path(self) -> str:
        return os.path.join(self.path, os.path.basename(self.testfile))

    @property
    def testfile_name(self) -> str:
        """Path to the testfile"""
        return os.path.basename(self.testfile)

    def add_artefacts(self) -> None:
        """Add artefacts required to build the Docker test image"""

        with open(self.dockerfile_path, "w") as f:
            self.stream(self.dockerfile)
            f.write(self.dockerfile)

        # add the testfile - distrotest.sh
        shutil.copyfile(self.testfile, self.testfile_path)

    async def build(self) -> None:
        """Add the required artefacts and build the Docker image for the test"""
        self.add_artefacts()
        try:
            await docker_utils.build_image(
                self.docker,
                self.path,
                self.tag,
                stream=self.stream,
                forcerm=True)
        except docker_utils.BuildError as e:
            raise BuildError(e.args[0])

    async def exists(self) -> bool:
        """Check if the Docker image exists already for the distribution"""
        return self.tag in await self.images()

    async def images(self) -> Iterable[str]:
        """The currently built Docker image tag names"""
        return itertools.chain.from_iterable(
            [image["RepoTags"] for image in await self.docker.images.list()])

    def installable_path(self, package_filename: str) -> str:
        """Path to the package inside the container"""
        # as this is a path inside the linux container - dont use `os`
        return f"{self.mount_install_dir}/{package_filename}"


class DistroTest(object):
    """A distribution <> package test

    Supplied `path` is the path to a (temporary) directory containing a
    `Dockerfile` and any artefacts required to build the distribution to test.

    The image is only built if it does not exist already.

    `installable` is the path to the package to test.

    The test starts the distro test container, and then `execs` the test script
    inside to run the actual tests.
    """

    def __init__(self, checker: checker.AsyncChecker, path: str, installable: str, name: str, image: str, tag: str):
        self.checker = checker
        self.path = path
        self.installable = installable
        self.distro = name
        self.build_image = image
        self.build_tag = tag

    @property
    def config(self) -> dict:
        """Docker container config"""
        # , HostConfig=dict(AutoRemove=True))
        return dict(Image=self.distro)

    @property
    def docker(self) -> aiodocker.Docker:
        """aiodocker.Docker connection"""
        return self.checker.docker

    @property
    def environment(self) -> dict:
        """Docker exec environment for the test"""
        return {}

    @property
    def errors(self) -> dict:
        return self.checker.errors

    @property
    def exiting(self) -> dict:
        return self.checker.exiting

    @cached_property
    def image(self) -> DistroTestImage:
        return self.image_class(
            self.docker,
            self.path,
            self.build_image,
            self.build_tag,
            self.testfile,
            self.distro,
            stream=self.stdout.info)

    @property
    def image_class(self):
        raise NotImplementedError

    @property
    def log(self) -> verboselogs.VerboseLogger:
        """A Logger to send progress information to"""
        return self.checker.log

    @cached_property
    def name(self) -> str:
        """The name of the Docker container used to test"""
        return "testing"

    @cached_property
    def package_filename(self) -> str:
        """The package filename"""
        return os.path.basename(self.installable)

    @cached_property
    def package_name(self) -> str:
        """The name of the package derived from the filename - eg `envoy-1.19`"""
        return self.package_filename.split("_")[0]

    @property
    def stdout(self) -> logging.Logger:
        """A Logger for raw logging"""
        return self.checker.stdout

    @property
    def test_cmd(self) -> tuple:
        """The test command to run inside the test container"""
        return (
            self.image.mount_testfile_path,
            self.image.installable_path(self.package_filename),
            self.package_name,
            self.distro)

    @property
    def testfile(self) -> str:
        """Path to the testfile"""
        return self.checker.testfile

    async def build(self) -> None:
        """Build the Docker image for the test with required artefacts"""
        if await self.image.exists():
            return
        self.run_log("Building image", msg_type="notice")
        await self.image.build()
        self.run_log("Image built")

    async def cleanup(self) -> None:
        """Attempt to kill the test container.

        As this is cleanup code, run when system is exiting, *ignore all errors*.
        """
        try:
            await self.stop(await self.docker.containers.get(self.name))
        finally:
            return

    async def create(self) -> aiodocker.containers.DockerContainer:
        """Create a Docker container for the test"""
        return await self.docker.containers.create_or_replace(config=self.config, name=self.name)

    async def exec(self, container: aiodocker.containers.DockerContainer) -> int:
        """Run Docker `exec` with the test"""
        execute = await container.exec(self.test_cmd, environment=self.environment)

        async with execute.start(detach=False) as stream:
            msg = await stream.read_out()
            _out = ""
            while msg:
                if _out:
                    self.handle_test_output(_out)
                _out = msg.data.decode("utf-8").strip()
                msg = await stream.read_out()

        return_code = (await execute.inspect())["ExitCode"]

        # this is not quite right yet...
        # the reason for using `_out` is to catch the situation where it outputs
        # one log and then fails - in that case we want to catch and log and not
        # just send it to stdout
        if _out:
            if return_code:
                # the test hasnt begun - log the error
                if not "distros" in self.errors:
                    self.error([f"[{self.distro}] Error executing test in container\n{_out}"])
            else:
                self.handle_test_output(_out)

        return return_code

    def error(self, errors: Union[list, tuple]) -> int:
        """Fail a test and log the errors"""
        return self.checker.error("distros", errors)

    def handle_test_output(self, msg: str) -> None:
        """Handle and log stream from test container

        if the message startswith eg `[debian_buster/envoy-19` then treat the
        message as a control message, otherwise log directly to stdout.

        if a control message contains `ERROR` then its treated as an error,
        and the test is marked as failed
        """

        if not msg.startswith(f"[{self.distro}"):
            # raw log
            self.stdout.info(msg)
            return

        if "ERROR" not in msg:
            # log informational message
            self.log.info(msg)
            return

        # testrun is eg `debian_buster/envoy-1.19`
        # testname is eg `proxy-responds`
        testrun, testname = msg.split("]")[0].strip("[").split(":")

        # fail the test, log an error, and output any extra `msg` content as
        # raw logs
        self.error([f"[{testrun}:{testname}] Test failed"])
        _msg = msg.split("\n", 1)
        if len(_msg) > 1:
            self.checker.stdout.error(_msg[1])

    def log_result(self, errors: tuple) -> None:
        failures = None
        if "distros" in self.checker.errors:
            # store/retrieve this internally
            failures = ",".join([
                fail.split("]")[0].strip("[").split(":")[1]
                for fail in self.checker.errors["distros"]
                if fail.startswith(f"[{self.distro}/{self.package_name}:")])

        if failures:
            self.checker.log.error(self.run_message(f"Package test had failures: {failures}", test=self.package_name))
        elif not errors:
            self.checker.succeed("distros", [self.run_message(f"Package test passed", test=self.package_name)])

    async def logs(self, container: aiodocker.containers.DockerContainer) -> str:
        return ''.join(log for log in await container.log(stdout=True, stderr=True))

    async def on_test_complete(self, container: aiodocker.containers.DockerContainer) -> Optional[Tuple[str]]:
        errors = await self.stop(container)
        self.log_result(errors)
        return errors

    async def run(self) -> Optional[Tuple[str]]:
        """Run the test"""
        container = None
        try:
            # build and start the container
            # if there are any Docker errors return them
            await self.build()
            container = await self.start()
        except aiodocker.exceptions.DockerError as e:
            return (e.args[1]["message"],)
        except (BuildError, ContainerError) as e:
            return e.args
        else:
            try:
                # execute the test in the container
                # if there are any Docker errors return them
                await self.exec(container)
            except aiodocker.exceptions.DockerError as e:
                return (e.args[1]["message"],)
        finally:
            errors = await self.on_test_complete(container)
        return errors

    def run_log(self, message: str, msg_type: str = "info", test: Optional[str] = None) -> None:
        getattr(self.log, msg_type)(self.run_message(message, test=test))

    def run_message(self, message: str, test: Optional[str] = None) -> None:
        return (f"[{self.distro}/{test}] {message}" if test else f"[{self.distro}] {message}")

    async def start(self) -> aiodocker.containers.DockerContainer:
        container = await self.create()
        await container.start()
        info = await container.show()
        if not info["State"]["Running"]:
            raise ContainerError(
                self.run_message(
                    f"Container unable to start\n{await self.logs(container)}", test=self.package_name))
        self.run_log("Container started", test=self.package_name)
        return container

    async def stop(
            self,
            container: Optional[aiodocker.containers.DockerContainer] = None) -> Optional[tuple]:
        """Stop the test container catching and returning any errors"""
        if not container:
            return
        try:
            await container.kill()
            await container.wait()
            await container.delete()
        except aiodocker.exceptions.DockerError as e:
            return (e.args[1]["message"],)
        self.run_log("Container stopped", test=self.package_name)


class DebDistroTestImage(DistroTestImage):
    package_type = "deb"

    @property
    def build_command(self) -> str:
        return DEB_BUILD_COMMAND

    @property
    def build_env(self) -> str:
        return DEB_ENV



class RPMDistroTestImage(DistroTestImage):
    package_type = "rpm"

    @property
    def build_command(self) -> str:
        return RPM_BUILD_COMMAND


class DebDistroTest(DistroTest):

    @property
    def image_class(self) -> Type[DistroTestImage]:
        return DebDistroTestImage

    @property
    def environment(self) -> dict:
        return dict(
            INSTALL_COMMAND=DEB_INSTALL_COMMAND,
            UNINSTALL_COMMAND=DEB_UNINSTALL_COMMAND,
            MAINTAINER_COMMAND=DEB_MAINTAINER_COMMAND)


class RPMDistroTest(DistroTest):

    @property
    def image_class(self) -> Type[DistroTestImage]:
        return RPMDistroTestImage

    @property
    def environment(self) -> dict:
        return dict(
            INSTALL_COMMAND=RPM_INSTALL_COMMAND,
            UNINSTALL_COMMAND=RPM_UNINSTALL_COMMAND,
            MAINTAINER_COMMAND=RPM_MAINTAINER_COMMAND)
