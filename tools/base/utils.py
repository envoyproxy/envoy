#
# Provides shared utils used by other python modules
#

import io
import os
import tarfile
import tempfile
from configparser import ConfigParser
from contextlib import ExitStack, contextmanager, redirect_stderr, redirect_stdout
from typing import Callable, Iterator, List, Optional, Union

import yaml


# this is testing specific - consider moving to tools.testing.utils
@contextmanager
def coverage_with_data_file(data_file: str) -> Iterator[str]:
    """This context manager takes the path of a data file
    and creates a custom coveragerc with the data file path included.

    The context is yielded the path to the custom rc file.
    """
    parser = ConfigParser()
    parser.read(".coveragerc")
    parser["run"]["data_file"] = data_file
    # use a temporary .coveragerc
    with tempfile.TemporaryDirectory() as tmpdir:
        tmprc = os.path.join(tmpdir, ".coveragerc")
        with open(tmprc, "w") as f:
            parser.write(f)
        yield tmprc


class BufferUtilError(Exception):
    pass


@contextmanager
def nested(*contexts):
    with ExitStack() as stack:
        yield [stack.enter_context(context) for context in contexts]


@contextmanager
def buffered(
        stdout: list = None,
        stderr: list = None,
        mangle: Optional[Callable[[list], list]] = None) -> Iterator[None]:
    """Captures stdout and stderr and feeds lines to supplied lists"""

    mangle = mangle or (lambda lines: lines)

    if stdout is None and stderr is None:
        raise BufferUtilError("You must specify stdout and/or stderr")

    contexts: List[Union[redirect_stderr[io.StringIO], redirect_stdout[io.StringIO]]] = []

    if stdout is not None:
        _stdout = io.StringIO()
        contexts.append(redirect_stdout(_stdout))
    if stderr is not None:
        _stderr = io.StringIO()
        contexts.append(redirect_stderr(_stderr))

    with nested(*contexts):
        yield

    if stdout is not None:
        _stdout.seek(0)
        stdout.extend(mangle(_stdout.read().strip().split("\n")))
    if stderr is not None:
        _stderr.seek(0)
        stderr.extend(mangle(_stderr.read().strip().split("\n")))


def extract(tarball: str, path: str) -> str:
    with tarfile.open(tarball) as tarfiles:
        tarfiles.extractall(path=path)
        return path


@contextmanager
def untar(tarball: str) -> Iterator[str]:
    """Untar a tarball into a temporary directory

    for example to list the contents of a tarball:

    ```
    import os

    from tooling.base.utils import untar


    with untar("path/to.tar") as tmpdir:
        print(os.listdir(tmpdir))

    ```

    the created temp directory will be cleaned up on
    exiting the contextmanager

    """
    with tempfile.TemporaryDirectory() as tmpdir:
        yield extract(tarball, tmpdir)


def from_yaml(path: str) -> Union[dict, list, str, int]:
    """Returns the loaded python object from a yaml file given by `path`"""
    with open(path) as f:
        return yaml.safe_load(f.read())


def to_yaml(data: Union[dict, list, str, int], path: str) -> str:
    """For given `data` dumps as yaml to provided `path`.

    Returns `path`
    """
    with open(path, "w") as f:
        f.write(yaml.dump(data))
    return path
