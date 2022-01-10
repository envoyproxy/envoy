import subprocess
from pathlib import Path
from typing import List, Union, Tuple

from tools.api_proto_breaking_change_detector.detector_errors import (
    ChangeDetectorError, ChangeDetectorInitializeError)
from envoy.base.utils import cd_and_return


def _generate_buf_args(target_path, config_file_loc, additional_args):
    buf_args = []

    # buf requires relative pathing for roots
    target_relative = Path(target_path).absolute().relative_to(Path.cwd().absolute())

    # buf does not accept . as a root; if we are already in the target dir, no need for a --path arg
    if str(target_relative) != ".":
        buf_args.extend(["--path", str(target_relative)])

    if config_file_loc:
        buf_args.extend(["--config", str(config_file_loc)])

    buf_args.extend(additional_args or [])

    return buf_args


def _cd_into_config_parent(config_file_loc):
    config_parent = Path(config_file_loc).parent if config_file_loc else Path.cwd()
    return cd_and_return(config_parent)


def pull_buf_deps(
        buf_path: Union[str, Path],
        target_path: Union[str, Path],
        config_file_loc: Union[str, Path] = None,
        additional_args: List[str] = None) -> None:
    """Updates buf.lock file and downloads any BSR dependencies specified in buf.yaml

    Note that in order for dependency downloading to trigger, `buf build`
    must be invoked, so `target_path` must contain valid proto syntax.

    Args:
        buf_path {Union[str, Path]} -- path to buf binary to use
        target_path {Union[str, Path]} -- path to directory containing protos to run buf on

        config_file_loc {Union[str, Path]} -- absolute path to buf.yaml configuration file (if not provided, uses default buf configuration)
        additional_args {List[str]} -- additional arguments passed into the buf binary invocations

    Raises:
        ChangeDetectorInitializeError: if buf encounters an error while attempting to update the buf.lock file or build afterward
    """
    with _cd_into_config_parent(config_file_loc):
        buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

        response = subprocess.run([buf_path, "mod", "update"],
                                  encoding="utf-8",
                                  capture_output=True)
        update_code, update_err = response.returncode, response.stderr.split("\n")
        # for some reason buf prints out the "downloading..." lines on stderr
        if update_code != 0:
            raise ChangeDetectorInitializeError(
                f"Error running `buf mod update`: exit status code {update_code} | stderr: {''.join(update_err)}"
            )
        if not Path.cwd().joinpath("buf.lock").exists():
            raise ChangeDetectorInitializeError(
                "buf mod update did not generate a buf.lock file (silent error... incorrect config?)"
            )

        subprocess.run([buf_path, "build"] + buf_args, capture_output=True)


def check_breaking(
        buf_path: Union[str, Path],
        target_path: Union[str, Path],
        git_ref: str,
        git_path: Union[str, Path],
        config_file_loc: Union[str, Path] = None,
        additional_args: List[str] = None,
        subdir: str = None) -> Tuple[int, List[str], List[str]]:
    """Runs `buf breaking` to check for breaking changes between the `target_path` protos and the provided initial state

    Args:
        buf_path {Union[str, Path]} -- path to buf binary to use
        target_path {Union[str, Path]} -- path to directory containing protos to check for breaking changes
        git_ref {str} -- git reference to use for the initial state of the protos (typically a commit hash)
        git_path {Union[str, Path]} -- absolute path to .git folder for the repository of interest

        subdir {str} -- subdirectory within git repository from which to search for .proto files (default: None, e.g. stay in root)
        config_file_loc {Union[str, Path]} -- absolute path to buf.yaml configuration file (if not provided, uses default buf configuration)
        additional_args {List[str]} -- additional arguments passed into the buf binary invocations

    Returns:
        Tuple[int, List[str], List[str]] -- tuple of (exit status code, stdout, stderr). Note stdout/stderr are provided as string lists
    """
    with _cd_into_config_parent(config_file_loc):
        if not Path(git_path).exists():
            raise ChangeDetectorError(f'path to .git folder {git_path} does not exist')

        buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

        initial_state_input = f'{git_path}#ref={git_ref}'

        if subdir:
            initial_state_input += f',subdir={subdir}'

        response = subprocess.run([buf_path, "breaking", "--against", initial_state_input]
                                  + buf_args,
                                  encoding="utf-8",
                                  capture_output=True)
        return response.returncode, response.stdout.split("\n"), response.stderr.split("\n")
