import logging
import pathlib
import re
import shutil
from functools import cached_property
from itertools import chain
from typing import Callable, Iterable, List, Optional, Tuple, Type, Union

import verboselogs

import aiodocker

from tools.base import checker, utils
from tools.docker import utils as docker_utils

DISTROTEST_CONFIG_PATH = "tools/distribution/distrotest.yaml"

DOCKER_IMAGE_PREFIX = "envoybuild_"
DOCKER_CONTAINER_PREFIX = "envoytest_"
DOCKERFILE_TEMPLATE = """
FROM {build_image}
{env}

ADD {install_dir} {install_mount_path}
ADD {testfile_name} {test_mount_path}
ADD {keyfile_name} {key_mount_path}
RUN {build_command}

CMD ["tail", "-f", "/dev/null"]
"""


class BuildError(Exception):
    pass


class ConfigurationError(Exception):
    pass


class ContainerError(Exception):
    pass


class DistroTestConfig(object):
    """Configuration object for distro tests

    This holds the configuration (and Docker connect) across a batch of tests.

    It also allows `DistroTest` to adapt `checker.Checker`.

    Init parameters:

    `path` is the path to the directory that will be used as the Docker context.
    `tarball` is the path to a tarball containing packages.
    `keyfile` is the path to the public key used to sign the packages
    `testfile` is the bash script to run inside the test containers.
    `maintainer` is the expected maintainer/packager the packages were signed with.
    `version` is the expected version of the packages.
    `config_path` allows you to override the default `distrotest.yaml`
    """

    packages_name = "packages"

    def __init__(
            self,
            docker: aiodocker.Docker,
            path: pathlib.Path,
            tarball: pathlib.Path,
            keyfile: pathlib.Path,
            testfile: pathlib.Path,
            maintainer: str,
            version: str,
            config_path: Optional[pathlib.Path] = None):
        self.docker = docker
        self._keyfile = keyfile
        self.path = path
        self.tarball = tarball
        self._testfile = testfile
        self.maintainer = maintainer
        self.version = version
        self._config_path = config_path

    def __getitem__(self, k):
        return self.config[k]

    @cached_property
    def config(self) -> dict:
        """Configuration for test types - eg deb/rpm

        This contains build information for different types of test image,
        and some defaults for specific test configuration.
        """
        return utils.from_yaml(self.config_path)

    @cached_property
    def config_path(self) -> pathlib.Path:
        """Path to a test configuration file"""
        return pathlib.Path(self._config_path or DISTROTEST_CONFIG_PATH)

    @cached_property
    def ctx_dockerfile(self) -> pathlib.Path:
        """Path to the Dockerfile in the Docker context"""
        return self.path.joinpath("Dockerfile")

    @cached_property
    def ctx_keyfile(self) -> pathlib.Path:
        """Path to the keyfile in the Docker context"""
        return self.path.joinpath(self._keyfile.name)

    @cached_property
    def rel_ctx_packages(self) -> pathlib.Path:
        """Path to the directory (in the Docker context) containing packages to
        test
        """
        return self.path.joinpath(self.packages_name)

    @cached_property
    def ctx_testfile(self) -> pathlib.Path:
        """Path to the testfile in the Docker context"""
        return self.path.joinpath(self._testfile.name)

    @cached_property
    def images(self) -> dict:
        """Mapping of images -> ext/types

        eg `debian` -> type=`deb` ext=`changes`
           `registry.access.redhat.com/ubi8/ubi` -> type=`rpm` ext=`rpm`

        for each image:
          - the `type` is used to find the directory of packages.
          - the `ext` is used to find packages within the directory.

        """
        return dict(
            chain.from_iterable(((image, dict(type=k, ext=v["ext"]))
                                 for image in v["images"])
                                for k, v in self.items()))

    @cached_property
    def install_img_path(self) -> pathlib.PurePosixPath:
        """Path to the install directory within the image/container"""
        return pathlib.PurePosixPath("/tmp/install")

    @cached_property
    def keyfile(self) -> pathlib.Path:
        """Path to the keyfile in the Docker context

        Copies the keyfile to the path on first access.
        """
        # Add the keyfile and return the path
        return shutil.copyfile(self._keyfile, self.ctx_keyfile)

    @cached_property
    def keyfile_img_path(self) -> pathlib.PurePosixPath:
        """Path to the public key of the key used to sign the packages, inside
        the Docker image/container
        """
        return pathlib.PurePosixPath("/tmp/gpg/signing.key")

    @cached_property
    def packages_dir(self) -> pathlib.Path:
        """The directory containing packages.

        Packages are extracted on first access
        """
        return utils.extract(self.rel_ctx_packages, self.tarball)

    @cached_property
    def testfile(self) -> pathlib.Path:
        """Path to the testfile in the Docker context

        Copies the testfile to the path on first access.
        """
        # Add the testfile - distrotest.sh - and return the path
        return shutil.copyfile(self._testfile, self.ctx_testfile)

    @cached_property
    def testfile_img_path(self) -> pathlib.PurePosixPath:
        """Path to the testfile within the image/container"""
        return pathlib.PurePosixPath("/tmp").joinpath(self.testfile.name)

    def get_config(self, image: str) -> dict:
        """Return the type/ext config for a particular image

        If the full image name - ie `image:tag` is provided, the `tag` is removed.
        """
        return self.images[self.get_image_name(image)]

    def get_image_name(self, image: str) -> str:
        """Get the image part of a full Docker image tag
        eg `debian:buster-slim` -> `debian`.
        """
        return image.split(":")[0]

    def get_package_type(self, image: str) -> str:
        """Get the package type for a particular image
        eg `debian:buster-slim` will resolve to `deb`

        If it cannot resolve a type from the configuration in `distrotest.yaml`
        it raises a `ConfigurationError`
        """
        image = self.get_image_name(image)
        for k, v in self.items():
            if image in v["images"]:
                return k
        raise ConfigurationError(f"Unrecognized image: {image}")

    def get_packages(self, type: str, ext: str) -> List[pathlib.Path]:
        """List of packages of a given type/ext found for testing"""
        return list(self.packages_dir.joinpath(type).glob(f"*.{ext}"))

    def items(self):
        return self.config.items()


class DistroTestImage(object):
    """A Docker image for running tests

    The image is installed with some basic utilities for testing.

    The image can be built if required.

    The built image also contains:

    - `self.dockerfile` - the `Dockerfile` build instructions
    - `self.keyfile` - the path to a populated file containing the package
        maintainer's public key.
    - `self.testfile` - the path to a populated file containing the test script.

    These are loaded into the Docker context when building.

    Init paramaters:

    `build_image`: the image to build - eg debian/buster-slim
    `name`: name to give the built image - eg `debian_buster`
    `stream`: optional callable to stream Docker output to
    """

    def __init__(
            self,
            test_config: DistroTestConfig,
            build_image: str,
            name: str,
            stream: Optional[Callable] = None):
        self.test_config = test_config
        self.build_image = build_image
        self.name = name
        self._stream = stream

    @property
    def build_command(self) -> str:
        """Command to build the Docker image"""
        return self.config["build"]["command"].strip().replace("\n", " && ")

    @cached_property
    def config(self) -> dict:
        """Config specific to this type of Docker image"""
        return self.test_config[self.package_type]

    @property
    def ctx_dockerfile(self) -> pathlib.Path:
        return self.test_config.ctx_dockerfile

    @property
    def ctx_install_dir(self) -> pathlib.Path:
        """Directory containing packages

        *relative to the Docker context root*
        """
        return pathlib.Path(self.packages_name).joinpath(self.package_type)

    @property
    def docker(self) -> aiodocker.Docker:
        return self.test_config.docker

    @cached_property
    def dockerfile(self) -> str:
        """The contents of the build Dockerfile"""
        return self.dockerfile_template.format(
            build_image=self.build_image,
            env=self.env,
            build_command=self.build_command,
            install_dir=self.ctx_install_dir,
            install_mount_path=self.install_img_path,
            testfile_name=self.testfile.name,
            test_mount_path=self.testfile_img_path,
            keyfile_name=self.keyfile.name,
            key_mount_path=self.keyfile_img_path)

    @property
    def dockerfile_template(self) -> str:
        """Dockerfile template"""
        return DOCKERFILE_TEMPLATE

    @property
    def env(self) -> str:
        """The `ENV` string to use in the `Dockerfile`"""
        _env = self.config["build"].get("env", "")
        return f"ENV {_env}" if _env else ""

    @property
    def install_img_path(self) -> pathlib.PurePosixPath:
        return self.test_config.install_img_path

    @property
    def keyfile_img_path(self) -> pathlib.PurePosixPath:
        return self.test_config.keyfile_img_path

    @property
    def keyfile(self) -> pathlib.Path:
        return self.test_config.keyfile

    @cached_property
    def package_type(self) -> str:
        return self.test_config.get_package_type(self.build_image)

    @property
    def packages_name(self) -> str:
        return self.test_config.packages_name

    @property
    def path(self) -> pathlib.Path:
        return self.test_config.path

    @property
    def prefix(self) -> str:
        """Prefix for the Docker image name that we be built"""
        return DOCKER_IMAGE_PREFIX

    @cached_property
    def tag(self) -> str:
        """Tag for the Docker test image build"""
        return f"{self.prefix}{self.name}:latest"

    @property
    def testfile(self) -> pathlib.Path:
        return self.test_config.testfile

    @property
    def testfile_img_path(self) -> pathlib.PurePosixPath:
        return self.test_config.testfile_img_path

    def add_dockerfile(self) -> None:
        """Add the Dockerfile for the test Docker image"""
        self.stream(self.dockerfile)
        self.ctx_dockerfile.write_text(self.dockerfile)

    async def build(self) -> None:
        """Build the Docker image for the test"""
        self.add_dockerfile()
        try:
            await docker_utils.build_image(
                self.docker, self.path, self.tag, stream=self.stream, forcerm=True)
        except docker_utils.BuildError as e:
            raise BuildError(e.args[0])

    async def exists(self) -> bool:
        """Check if the Docker image exists already for the distribution"""
        return self.tag in await self.images()

    def get_environment(self, package_filename: str, package_name: str, name: str) -> dict:
        """Creates a dictionary of environment variables that are injected when
        the test is `exec`ed

        Defaults are added from the global test configuration
        (ie `distrotest.yaml`), the package `ext` can be overridden by the
        passed in `yaml` test config file.

        Each var is formatted with the existing env dict, so you can
        interpolate any previously defined vars.
        """
        env = dict(
            ENVOY_MAINTAINER=self.test_config.maintainer,
            ENVOY_VERSION=self.test_config.version,
            ENVOY_INSTALL_BINARY=self.installable_img_path(
                self.get_install_binary(package_filename)),
            ENVOY_INSTALLABLE=self.installable_img_path(package_filename),
            PACKAGE=package_name,
            DISTRO=name)
        for k, v in self.config["test"].items():
            env[k.upper()] = v.format(**env)
        return env

    def get_install_binary(self, package: str) -> str:
        """Get the name of the installation binary from the installable file.

        For debian this will be the `.deb` file associated with the installable `.changes` file.

        For redhat its just the `.rpm`
        """
        return (
            re.sub(
                self.config["binary_name"]["match"], self.config["binary_name"]["replace"], package)
            if "binary_name" in self.config else package)

    async def images(self) -> Iterable[str]:
        """The currently built Docker image tag names"""
        return chain.from_iterable([image["RepoTags"] for image in await self.docker.images.list()])

    def installable_img_path(self, package_filename: str) -> pathlib.PurePosixPath:
        """Path to a package inside the container"""
        return self.install_img_path.joinpath(package_filename)

    def stream(self, msg: str) -> None:
        if self._stream:
            self._stream(msg)


class DistroTest(object):
    """A distribution <> package test

    The test image is only built if it does not exist already.

    The test starts the distro test container with the test image, and then
    `execs` the test script inside the container to run the tests.

    Init parameters:

    `name`: the distro test name - eg `redhat_8.3`
    `image`: the test image - eg `registry.access.redhat.com/ubi8/ubi:8.3`
    `installable`: is the path to the actual package to test.
    `rebuild`: flag to rebuild the image if it exists
    """

    def __init__(
            self,
            checker: checker.AsyncChecker,
            test_config: DistroTestConfig,
            name: str,
            image: str,
            installable: pathlib.Path,
            rebuild: bool = False):
        self.checker = checker
        self.test_config = test_config
        self.installable = installable
        self.distro = name
        self.build_image = image
        self.rebuild = rebuild
        self._failures = []

    @property
    def config(self) -> dict:
        """Docker container config"""
        # Dont use `AutoRemove` as we want the logs from failed containers
        return dict(Image=self.image.tag)

    @property
    def docker(self) -> aiodocker.Docker:
        """aiodocker.Docker connection"""
        return self.test_config.docker

    @property
    def environment(self) -> dict:
        """Docker exec environment for the test"""
        return self.image.get_environment(self.installable.name, self.package_name, self.distro)

    @property
    def errors(self) -> dict:
        """Dictionary of test errors stored on the provided Checker"""
        return self.checker.errors

    @property
    def exiting(self) -> bool:
        """Flag to indicate that the program is exiting due to
        `KeyboardInterrupt`
        """
        return self.checker.exiting

    @property
    def failed(self) -> bool:
        """Flag to indicate whether there are test failures from running
        the test inside the container
        """
        return len(self.failures) > 0

    @property
    def failures(self) -> list:
        """List of test failures from running the test inside the container
        """
        return self._failures

    @cached_property
    def image(self) -> DistroTestImage:
        """A Docker image used for testing that can be built if required"""
        return self.image_class(
            self.test_config, self.build_image, self.distro, stream=self.stdout.info)

    @property
    def image_class(self) -> Type[DistroTestImage]:
        return DistroTestImage

    @property
    def log(self) -> verboselogs.VerboseLogger:
        """A logger to send progress information to"""
        return self.checker.log

    @cached_property
    def name(self) -> str:
        """The name of the Docker container used to test"""
        return f"{self.prefix}{self.distro}"

    @cached_property
    def package_name(self) -> str:
        """The name of the package derived from the filename - eg `envoy-1.19`"""
        return self.installable.name.split("_")[0]

    @property
    def prefix(self) -> str:
        """Prefix for the container name"""
        return DOCKER_CONTAINER_PREFIX

    @property
    def stdout(self) -> logging.Logger:
        """A logger for raw logging"""
        return self.checker.stdout

    @property
    def test_cmd(self) -> tuple:
        """The test command to run inside the test container"""
        return (str(self.test_config.testfile_img_path),)

    @property
    def testfile(self) -> pathlib.Path:
        """Path to the testfile"""
        return self.test_config.testfile

    async def build(self) -> None:
        """Build the Docker image for the test if required"""
        if not self.rebuild and await self.image.exists():
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

    async def exec(self, container: aiodocker.containers.DockerContainer) -> None:
        """Run Docker `exec` with the test"""
        execute = await container.exec(self.test_cmd, environment=self.environment)

        # The reason for using `_out` here is to catch the situation where it
        # outputs one log and then fails before any tests have run
        # in that case we want to catch and log the error and not just send it to
        # stdout
        async with execute.start(detach=False) as stream:
            msg = await stream.read_out()
            _out = ""
            while msg:
                if _out:
                    self.handle_test_output(_out)
                _out = msg.data.decode("utf-8").strip()
                msg = await stream.read_out()

        # We only log an error if `exec` failed and there are no test failures
        return_code = (await execute.inspect())["ExitCode"]
        _log_error = _out and return_code and not self.failed
        if _log_error:
            self._failures.append("container-start")
            self.error([f"[{self.distro}] Error executing test in container\n{_out}"])
        elif _out:
            self.handle_test_output(_out)

    def error(self, errors: Union[list, tuple]) -> int:
        """Fail a test and log the errors"""
        return self.checker.error(self.checker.active_check, errors)

    def handle_test_error(self, msg: str) -> None:
        """Handle a test error

        Any "control" lines in the test that contain `ERROR` will cause the
        test to fail and any additional lines are output to stderr.
        """
        # testrun is eg `debian_buster/envoy-1.19`
        # testname is eg `proxy-responds`
        testrun, testname = msg.split("]")[0].strip("[").split(":")

        # Record the failure for summarizing
        self._failures.append(testname)

        # Fail the test, log an error, and output any extra `msg` content as
        # raw logs
        self.error([f"[{testrun}:{testname}] Test failed"])
        _msg = msg.split("\n", 1)
        if len(_msg) > 1:
            self.stdout.error(_msg[1])

    def handle_test_output(self, msg: str) -> None:
        """Handle and log stream from test container

        If the message startswith eg `[debian_buster/envoy-19` then treat the
        message as a control message, otherwise log directly to stdout.

        If a control message contains `ERROR` then its treated as an error,
        and the test is marked as failed

        If a non-control message contains `\n` then the first line is split
        and output, and the method recurses with the remainder.
        """
        if not msg.startswith(f"[{self.distro}"):
            if "\n" not in msg:
                # raw log
                self.stdout.info(msg)
                return

            # Sometimes lines come joined together. This handles that,
            # and prevents control messages being missed.
            _msg = msg.split("\n", 1)
            self.stdout.info(_msg[0])
            self.handle_test_output(_msg[1])
            return

        if "ERROR" not in msg:
            # Log informational message
            self.log.info(msg)
            return
        self.handle_test_error(msg)

    def log_failures(self) -> None:
        """Log a failure summary of a test"""
        if not self.failed:
            return
        self.run_log(
            f"Package test had failures: {','.join(self.failures)}",
            msg_type="error",
            test=self.package_name)

    async def logs(self, container: aiodocker.containers.DockerContainer) -> str:
        """Return the concatenated container logs, only called if the container fails to start"""
        return ''.join(await container.log(stdout=True, stderr=True))

    async def on_test_complete(self, container: aiodocker.containers.DockerContainer,
                               failed: bool) -> Optional[Tuple[str]]:
        """Stop the container and record the results"""
        self.log_failures()
        await self.stop(container)
        if not (failed or self.failed):
            self.checker.succeed(
                self.checker.active_check,
                [self.run_message(f"Package test passed", test=self.package_name)])

    async def run(self) -> None:
        """Run the test - build and start the container, and then exec the test inside"""
        self.error(await self._run())

    def run_log(self, message: str, msg_type: str = "info", test: Optional[str] = None) -> None:
        """Log a message with test prefix"""
        getattr(self.log, msg_type)(self.run_message(message, test=test))

    def run_message(self, message: str, test: Optional[str] = None) -> str:
        """A log message with relevant test prefix"""
        return f"[{self.distro}/{test}] {message}" if test else f"[{self.distro}] {message}"

    async def start(self) -> aiodocker.containers.DockerContainer:
        """Start and return the test container, error if it fails to start"""
        container = await self.create()
        await container.start()
        info = await container.show()
        if not info["State"]["Running"]:
            raise ContainerError(
                self.run_message(
                    f"Container unable to start\n{await self.logs(container)}",
                    test=self.package_name))
        self.run_log("Container started", test=self.package_name)
        return container

    async def stop(self, container: Optional[aiodocker.containers.DockerContainer] = None) -> None:
        """Stop the test container"""
        if not container:
            return
        await container.kill()
        await container.delete()
        self.run_log("Container stopped", test=self.package_name)

    async def _run(self) -> Optional[Tuple[str]]:
        container = None
        # As `finally` is always called, regardless of any errors being
        # raised, we assume that something failed, unless build/start/exec
        # complete without raising an error.
        # actual test failures are recorded separately
        failed = True
        try:
            # build, start and exec the container
            await self.build()
            container = await self.start()
            await self.exec(container)
            failed = False
        except (BuildError, ConfigurationError, ContainerError) as e:
            # Catch build/start/exec Docker errors and return
            return e.args
        except aiodocker.exceptions.DockerError as e:
            # If there are any other Docker errors return the error message
            return (e.args[1]["message"],)
        finally:
            # Stop the container and handle success/failure
            try:
                await self.on_test_complete(container, failed)
                errors = None
            except aiodocker.exceptions.DockerError as e:
                # capture Docker errors from trying to stop the container
                errors = (e.args[1]["message"],)
        # Return errors from trying to stop the container if any
        return errors
