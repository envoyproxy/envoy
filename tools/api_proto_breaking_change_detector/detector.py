from abc import ABC, abstractmethod
import tempfile
from rules_python.python.runfiles import runfiles
from tools.run_command import run_command
from shutil import copyfile
import re
from pathlib import Path
import os
"""
This tool is used to detect "breaking changes" in protobuf files, to ensure proper backwards-compatibility in protobuf API updates.
The tool can check for breaking changes of a single API by taking 2 .proto file paths as input (before and after) and outputting a bool `is_breaking`.

The breaking change detector creates a temporary directory, copies in each file to compute a protobuf "state",
computes a diff of the "before" and "after" states, and runs the diff against a set of rules to determine if there was a breaking change.

The tool is currently implemented with buf (https://buf.build/)
"""


# generic breaking change detector for protos, extended by a wrapper class for a breaking change detector
class ProtoBreakingChangeDetector(ABC):

    @abstractmethod
    def __init__(self, path_to_before, path_to_after, additional_args=None):
        pass

    @abstractmethod
    def run_detector(self):
        pass

    @abstractmethod
    def is_breaking(self):
        pass

    @abstractmethod
    def lock_file_changed(self):
        pass


class ChangeDetectorError(Exception):
    pass


class ChangeDetectorInitializeError(ChangeDetectorError):
    pass


BUF_LOCK_FILE = "tmp.json"


class BufWrapper(ProtoBreakingChangeDetector):

    def __init__(self, path_to_before, path_to_after, additional_args=None):
        if not Path(path_to_before).is_file():
            raise ValueError(f"path_to_before {path_to_before} does not exist")

        if not Path(path_to_after).is_file():
            raise ValueError(f"path_to_after {path_to_after} does not exist")

        self.path_to_before = path_to_before
        self.path_to_after = path_to_after
        self.additional_args = additional_args

    def run_detector(self):
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

        target = Path(temp_dir.name, f"{Path(self.path_to_before).stem}.proto")

        buf_args = [
            "--path",
            str(target.relative_to(
                Path(".").absolute())),  # buf requires relative pathing for roots...
            "--config",
            str(yaml_file_loc),
        ]
        if self.additional_args is not None:
            buf_args.extend(self.additional_args)

        copyfile(self.path_to_before, target)

        initial_code, initial_out, initial_err = run_command(
            ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

        if initial_code != 0 or len(initial_out) > 0 or len(initial_err) > 0:
            raise ChangeDetectorInitializeError("Unexpected error during init")

        lock_location = Path(temp_dir.name, BUF_LOCK_FILE)
        with open(lock_location) as f:
            initial_lock = f.readlines()

        copyfile(self.path_to_after, target)

        final_code, final_out, final_err = run_command(
            ' '.join(
                [buf_path, f"breaking --against {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        final_out, final_err = ''.join(final_out), ''.join(final_err)

        # new with buf: lock must be manually re-built
        # but here we only re-build if there weren't any detected breaking changes
        if len(final_out) == len(final_err) == final_code == 0:
            _, _, _ = run_command(
                ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_LOCK_FILE)}", *buf_args]))
        with open(lock_location) as f:
            final_lock = f.readlines()

        temp_dir.cleanup()

        self.initial_result = initial_code, initial_out, initial_err
        self.final_result = final_code, final_out, final_err
        self.initial_lock = initial_lock
        self.final_lock = final_lock

    def is_breaking(self):
        final_code, final_out, final_err = self.final_result

        # Ways buf output could be indicative of a breaking change:
        # 1) run_command code is not 0 (e.g. it's 100)
        # 2) stdout/stderr is nonempty
        # 3) stdout/stderr contains "Failure"
        break_condition = lambda inp: len(inp) > 0 or bool(re.match(r"Failure", inp))
        return final_code != 0 or break_condition(final_out) or break_condition(final_err)

    def lock_file_changed(self):
        return any(before != after for before, after in zip(self.initial_lock, self.final_lock))
