from pathlib import Path
from tools.run_command import run_command
from detector_errors import ChangeDetectorError, ChangeDetectorInitializeError
import os


def _generate_buf_args(target_path, config_file_loc, additional_args):
    buf_args = []

    # buf requires relative pathing for roots
    target_relative = Path(target_path).absolute().relative_to(Path(".").absolute())

    # buf does not accept . as a root; if we are already in the target dir, no need for a --path arg
    if target_relative != Path("."):
        buf_args.extend(["--path", str(target_relative)])

    if config_file_loc:
        buf_args.extend(["--config", str(config_file_loc)])

    buf_args.extend(additional_args or [])

    return buf_args


def pull_buf_deps(buf_path, target_path, config_file_loc=None, additional_args=None):
    if config_file_loc:
        os.chdir(Path(config_file_loc).parent)

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


def make_lock(buf_path, target_path, lock_file_path, config_file_loc=None, additional_args=None):
    buf_args = _generate_buf_args(target_path, config_file_loc, additional_args)

    initial_code, initial_out, initial_err = run_command(
        ' '.join([buf_path, f"build -o {lock_file_path}", *buf_args]))
    initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

    if initial_code != 0 or len(initial_out) > 0 or len(initial_err) > 0:
        raise ChangeDetectorInitializeError(
            f"Unexpected error during init:\n\tExit Status Code: {initial_code}\n\tstdout: {initial_out}\n\t stderr: {initial_err}\n"
        )
    return initial_code, initial_out, initial_err


def check_breaking(
        buf_path,
        target_path,
        config_file_loc=None,
        additional_args=None,
        lock_file_path=None,
        git_ref=None,
        git_path=None):
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
