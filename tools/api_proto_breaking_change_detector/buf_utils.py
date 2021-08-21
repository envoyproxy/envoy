import os
from pathlib import Path
from typing import List, Union, Tuple

from detector_errors import ChangeDetectorError, ChangeDetectorInitializeError
from tools.base.utils import cd_and_return
from tools.run_command import run_command


def _generate_buf_args(target_path, config_file_loc, additional_args):
    buf_args = []

    # buf requires relative pathing for roots
    target_relative = Path(target_path).absolute().relative_to(Path.cwd().absolute())

    # buf does not accept . as a root; if we are already in the target dir, no need for a --path arg
    if target_relative != Path.cwd():
        buf_args.extend(["--path", str(target_relative)])

    if config_file_loc:
        buf_args.extend(["--config", str(config_file_loc)])

    buf_args.extend(additional_args or [])

    return buf_args


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
    config_parent = Path(config_file_loc).parent if config_file_loc else Path.cwd()
    with cd_and_return(config_parent):
        buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

        update_code, _, update_err = run_command(f'{buf_path} mod update')
        # for some reason buf prints out the "downloading..." lines on stderr
        if update_code != 0:
            raise ChangeDetectorInitializeError(
                f'Error running `buf mod update`: exit status code {update_code} | stderr: {update_err}'
            )

        build_code, build_out, build_err = run_command(' '.join([f'{buf_path} build', *buf_args]))
        build_out, build_err = ''.join(build_out), ''.join(build_err)
        if build_code != 0:
            raise ChangeDetectorInitializeError(
                f'Error running `buf build` after updating deps: exit status code {build_code} | stdout: {build_out} | stderr: {build_err}'
            )


def make_lock(
        buf_path: Union[str, Path],
        target_path: Union[str, Path],
        lock_file_path: Union[str, Path],
        config_file_loc: Union[str, Path] = None,
        additional_args: List[str] = None) -> Tuple[int, str, str]:
    """Builds a buf image ("lock file") to given lock file path using given target protos

    Note that lock_file_path file extension must be one of [.json, .bin] as required by buf

    Args:
        buf_path {Union[str, Path]} -- path to buf binary to use
        target_path {Union[str, Path]} -- path to directory containing protos to build image from
        lock_file_path {Union[str, Path]} -- path for output lock file

        config_file_loc {Union[str, Path]} -- absolute path to buf.yaml configuration file (if not provided, uses default buf configuration)
        additional_args {List[str]} -- additional arguments passed into the buf binary invocations

    Raises:
        ChangeDetectorError: If buf encounters an error when building the image

    Returns:
        Tuple[int, str, str] -- tuple of (exit status code, stdout, stderr) as provided by run_command. Note stdout/stderr are joined as single strings
    """
    buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

    initial_code, initial_out, initial_err = run_command(
        ' '.join([buf_path, f"build -o {lock_file_path}", *buf_args]))
    initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

    if initial_code != 0 or len(initial_out) > 0 or len(initial_err) > 0:
        raise ChangeDetectorError(
            f"Unexpected error during init:\n\tExit Status Code: {initial_code}\n\tstdout: {initial_out}\n\t stderr: {initial_err}\n"
        )
    return initial_code, initial_out, initial_err


def check_breaking(
        buf_path: Union[str, Path],
        target_path: Union[str, Path],
        config_file_loc: Union[str, Path] = None,
        additional_args: List[str] = None,
        lock_file_path: Union[str, Path] = None,
        git_ref: str = None,
        git_path: Union[str, Path] = None) -> Tuple[int, List[str], List[str]]:
    """Runs `buf breaking` to check for breaking changes between the `target_path` protos and the provided initial state

    Args:
        buf_path {Union[str, Path]} -- path to buf binary to use
        target_path {Union[str, Path]} -- path to directory containing protos to check for breaking changes

        config_file_loc {Union[str, Path]} -- absolute path to buf.yaml configuration file (if not provided, uses default buf configuration)
        additional_args {List[str]} -- additional arguments passed into the buf binary invocations

        If using lock file mode:
            lock_file_path {Union[str, Path]} -- absolute path to the lock file representing the initial state (must be provided if using lock file mode)
        If using git ref mode:
            git_ref {str} -- git reference to use for the initial state of the protos (typically a commit hash) (must be provided if using git mode)
            git_path {Union[str, Path]} -- absolute path to .git file for the repository of interest (must be provided if using git mode)

    Returns:
        Tuple[int, List[str], List[str]] -- tuple of (exit status code, stdout, stderr) as provided by run_command. Note stdout/stderr are provided as string lists
    """
    if bool(lock_file_path) == bool(git_ref):
        raise ChangeDetectorError(
            "Expecting either a path to a lock file or a git ref, but not both")
    if bool(git_ref) != bool(git_path):
        raise ChangeDetectorError("If using git mode, expecting a git ref and a path to .git file")

    buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

    initial_state_input = lock_file_path if lock_file_path else f'{git_path}#ref={git_ref},subdir=api'

    final_code, final_out, final_err = run_command(
        ' '.join([buf_path, f"breaking --against {initial_state_input}", *buf_args]))
    return final_code, final_out, final_err
