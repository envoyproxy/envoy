""" Protocol Buffer Breaking Change Detector

This tool is used to detect "breaking changes" in protobuf files, to
ensure proper backwards-compatibility in protobuf API updates. The tool
can check for breaking changes of a single API by taking 2 .proto file
paths as input (before and after) and outputting a bool `is_breaking`.

The breaking change detector creates a temporary directory, copies in
each file to compute a protobuf "state", computes a diff of the "before"
and "after" states, and runs the diff against a set of rules to determine
if there was a breaking change.

The tool is currently implemented with buf (https://buf.build/)
"""

import tempfile
from rules_python.python.runfiles import runfiles
from tools.run_command import run_command
from shutil import copyfile
from pathlib import Path
import os
from typing import List


class ProtoBreakingChangeDetector(object):
    """Abstract breaking change detector interface"""

    def __init__(self, path_to_before: str, path_to_after: str) -> None:
        """Initialize the configuration of the breaking change detector

        This function sets up any necessary config without actually
        running the detector against any proto files.

        Takes in a single protobuf as 2 files, in a ``before`` state
        and an ``after`` state, and checks if the ``after`` state
        violates any breaking change rules.

        Args:
            path_to_before {str} -- absolute path to the .proto file in the before state
            path_to_after {str} -- absolute path to the .proto file in the after state
        """
        pass

    def run_detector(self) -> None:
        """Run the breaking change detector to detect rule violations

        This method should populate the detector's internal data such
        that `is_breaking` and `lock_file_changed` do not require any
        additional invocations to the breaking change detector.
        """
        pass

    def is_breaking(self) -> bool:
        """Return True if breaking changes were detected in the given protos"""
        pass

    def lock_file_changed(self) -> bool:
        """Return True if the detector state file changed after being run

        This function assumes that the detector uses a lock file to
        compare "before" and "after" states of protobufs, which is
        admittedly an implementation detail. It is mostly used for
        testing, to ensure that the breaking change detector is
        checking all of the protobuf features we are interested in.
        """
        pass


class ChangeDetectorError(Exception):
    pass


class ChangeDetectorInitializeError(ChangeDetectorError):
    pass


BUF_STATE_FILE = "tmp.json"


class BufWrapper(ProtoBreakingChangeDetector):
    """Breaking change detector implemented with buf"""

    def __init__(
            self,
            path_to_before: str,
            path_to_after: str,
            additional_args: List[str] = None) -> None:
        if not Path(path_to_before).is_file():
            raise ValueError(f"path_to_before {path_to_before} does not exist")

        if not Path(path_to_after).is_file():
            raise ValueError(f"path_to_after {path_to_after} does not exist")

        self._path_to_before = path_to_before
        self._path_to_after = path_to_after
        self._additional_args = additional_args

    def run_detector(self) -> None:
        # buf requires protobuf files to be in a subdirectory of the yaml file
        with tempfile.TemporaryDirectory(prefix=str(Path(".").absolute()) + os.sep) as temp_dir:
            buf_path = runfiles.Create().Rlocation("com_github_bufbuild_buf/bin/buf")

            buf_config_loc = Path(".", "tools", "api_proto_breaking_change_detector")

            yaml_file_loc = Path(".", "buf.yaml")
            copyfile(Path(buf_config_loc, "buf.yaml"), yaml_file_loc)

            target = Path(temp_dir, f"{Path(self._path_to_before).stem}.proto")

            buf_args = [
                "--path",
                # buf requires relative pathing for roots
                str(target.relative_to(Path(".").absolute())),
                "--config",
                str(yaml_file_loc),
            ]
            buf_args.extend(self._additional_args or [])

            copyfile(self._path_to_before, target)

            lock_location = Path(temp_dir, BUF_STATE_FILE)

            initial_code, initial_out, initial_err = run_command(
                ' '.join([buf_path, f"build -o {lock_location}", *buf_args]))
            initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

            if initial_code != 0 or len(initial_out) > 0 or len(initial_err) > 0:
                raise ChangeDetectorInitializeError(
                    f"Unexpected error during init:\n\tExit Status Code: {initial_code}\n\tstdout: {initial_out}\n\t stderr: {initial_err}\n"
                )

            with open(lock_location, "r") as f:
                self._initial_lock = f.readlines()

            copyfile(self._path_to_after, target)

            final_code, final_out, final_err = run_command(
                ' '.join([buf_path, f"breaking --against {lock_location}", *buf_args]))
            final_out, final_err = ''.join(final_out), ''.join(final_err)

            if len(final_out) == len(final_err) == final_code == 0:
                _, _, _ = run_command(' '.join([buf_path, f"build -o {lock_location}", *buf_args]))
            with open(lock_location, "r") as f:
                self._final_lock = f.readlines()

            self._initial_result = initial_code, initial_out, initial_err
            self._final_result = final_code, final_out, final_err

    def is_breaking(self) -> bool:
        final_code, final_out, final_err = self._final_result

        if final_code != 0:
            return True
        if final_out != "" or "Failure" in final_out:
            return True
        if final_err != "" or "Failure" in final_err:
            return True
        return False

    def lock_file_changed(self) -> bool:
        return any(before != after for before, after in zip(self._initial_lock, self._final_lock))
