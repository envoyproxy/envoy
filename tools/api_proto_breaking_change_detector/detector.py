from abc import ABC, abstractmethod
import tempfile
from rules_python.python.runfiles import runfiles
from tools.run_command import run_command
from shutil import copyfile
import re
from pathlib import Path
import os


# generic breaking change detector for protos, extended by a wrapper class for a breaking change detector
class ProtoBreakingChangeDetector(ABC):
    # stateless
    @staticmethod
    def is_breaking(path_to_before, path_to_after):
        pass

    @staticmethod
    def lock_file_changed(path_to_before, path_to_after):
        pass


class ChangeDetectorError(Exception):
    pass


class ChangeDetectorInitializeError(ChangeDetectorError):
    pass


BUF_LOCK_FILE = "tmp.json"


class BufWrapper(ProtoBreakingChangeDetector):

    @staticmethod
    def _run_buf(path_to_before, path_to_after, additional_args=None):
        if not Path(path_to_before).is_file():
            raise ValueError(f"path_to_before {path_to_before} does not exist")

        if not Path(path_to_after).is_file():
            raise ValueError(f"path_to_after {path_to_after} does not exist")

        # 1) pull buf BSR dependencies with buf mod update (? still need to figure this out. commented out for now)
        # 2) copy buf.yaml into temp dir
        # 3) copy start file into temp dir
        # 4) buf build -o tmp_file
        # 5) copy changed file into temp dir
        # 6) buf breaking --against tmp_file
        # 7) check for differences (if changes are breaking, there should be none)

        temp_dir = tempfile.TemporaryDirectory(
            prefix=str(Path(".").absolute())
            + os.sep)  # buf requires protobuf files to be in a subdirectory of cwd
        buf_path = runfiles.Create().Rlocation("com_github_bufbuild_buf/bin/buf")

        buf_config_loc = Path(".", "tools", "api_proto_breaking_change_detector")

        #copyfile(Path(buf_config_loc, "buf.lock"), Path(".", "buf.lock")) # not needed? refer to comment below
        yaml_file_loc = Path(".", "buf.yaml")
        copyfile(Path(buf_config_loc, "buf.yaml"), yaml_file_loc)

        # TODO: figure out how to automatically pull buf deps
        # `buf mod update` doesn't seem to do anything, and the first test will fail because it forces buf to automatically start downloading the deps
        #        bcode, bout, berr = run_command(f"{buf_path} mod update")
        #        bout, berr = ''.join(bout), ''.join(berr)

        target = Path(temp_dir.name, f"{Path(path_to_before).stem}.proto")

        buf_args = [
            "--path",
            str(target.relative_to(
                Path(".").absolute())),  # buf requires relative pathing for roots...
            "--config",
            str(yaml_file_loc),
        ]
        if additional_args is not None:
            buf_args.extend(additional_args)

        copyfile(path_to_before, target)

        initial_code, initial_out, initial_err = run_command(
            ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

        if len(initial_out) > 0 or len(initial_err) > 0:
            raise ChangeDetectorInitializeError("Unexpected error during init")

        lock_location = Path(temp_dir.name, BUF_LOCK_FILE)
        with open(lock_location) as f:
            initial_lock = f.readlines()

        copyfile(path_to_after, target)

        final_code, final_out, final_err = run_command(
            ' '.join(
                [buf_path, f"breaking --against {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        final_out, final_err = ''.join(final_out), ''.join(final_err)

        # new with buf: lock must be manually re-built
        # but here we only re-build if there weren't any detected breaking changes
        if len(final_out) == len(final_err) == 0:
            _, _, _ = run_command(
                ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        with open(lock_location) as f:
            final_lock = f.readlines()

        temp_dir.cleanup()

        return (initial_code, initial_out,
                initial_err), (final_code, final_out, final_err), initial_lock, final_lock

    @staticmethod
    def is_breaking(path_to_before, path_to_after, additional_args=None):
        _, final_result, _, _ = BufWrapper._run_buf(path_to_before, path_to_after, additional_args)

        _, final_out, final_err = final_result

        # Ways buf output could be indicative of a breaking change:
        # 1) stdout/stderr is nonempty
        # 2) stdout/stderr contains "Failure"
        break_condition = lambda inp: len(inp) > 0 or bool(re.match(r"Failure", inp))
        return break_condition(final_out) or break_condition(final_err)

    @staticmethod
    def lock_file_changed(path_to_before, path_to_after, additional_args=None):
        _, _, initial_lock, final_lock = BufWrapper._run_buf(
            path_to_before, path_to_after, additional_args)
        return any(before != after for before, after in zip(initial_lock, final_lock))
