from abc import ABC, abstractmethod
import tempfile
from rules_python.python.runfiles import runfiles
from tools.run_command import run_command
from shutil import copyfile
import re
from pathlib import Path
import os
from typing import List
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
    def __init__(
            self,
            path_to_before: str,
            path_to_after: str,
            additional_args: List[str] = None) -> None:
        """Initializes the breaking change detector, setting up any necessary config without actually running
        the detector against any proto files.

        Takes in a single protobuf as 2 files, in a ``before`` state and an ``after`` state,
        and checks if the ``after`` state violates any breaking change rules.

        :param path_to_before: absolute path to the .proto file in the before state
        :type path_to_before: ``str``

        :param path_to_after: absolute path to the .proto file in the after state
        :type path_to_after: ``str``

        :param additional_args: additional arguments for the breaking change detector CLI. May be tool-dependent.
            Additional arguments are passed in both the "initialize" and "detect changes" steps.
        :type additional_args: ``List[str]``
        """
        pass

    @abstractmethod
    def run_detector(self) -> None:
        """Runs the breaking change detector with the parameters given in ``__init__``. This method should populate
        the detector's internal data such that `is_breaking` and `lock_file_changed` do not require any additional
        invocations to the breaking change detector.
        """
        pass

    @abstractmethod
    def is_breaking(self) -> bool:
        """Returns whether the changes between the ``before`` and ``after`` states of the proto file given in ``__init__``
        violate any breaking change rules specified by this breaking change detector.

        :return: a boolean flag indicating if the changes between ``before`` and ``after`` are breaking
        :rtype: ``bool``
        """
        pass

    @abstractmethod
    def lock_file_changed(self) -> bool:
        """Returns whether the changes between the ``before`` and ``after`` states of the proto file given in ``__init__``
        cause a change in the detector's state file. This function is mostly used for testing, to ensure that the breaking change 
        detector is checking all of the protobuf features we are interested in.

        :return: a boolean flag indicating if the changes between ``before`` and ``after`` cause a change in the state file
        :rtype: ``bool``
        """
        pass


class ChangeDetectorError(Exception):
    pass


class ChangeDetectorInitializeError(ChangeDetectorError):
    pass


BUF_STATE_FILE = "tmp.json"


class BufWrapper(ProtoBreakingChangeDetector):

    def __init__(
            self,
            path_to_before: str,
            path_to_after: str,
            additional_args: List[str] = None) -> None:
        """Initializes the breaking change detector, setting up any necessary config without actually running
        the detector against any proto files.

        Takes in a single protobuf as 2 files, in a ``before`` state and an ``after`` state,
        and checks if the ``after`` state violates any breaking change rules.

        :param path_to_before: absolute path to the .proto file in the before state
        :type path_to_before: ``str``

        :param path_to_after: absolute path to the .proto file in the after state
        :type path_to_after: ``str``

        :param additional_args: additional arguments for the breaking change detector CLI. May be tool-dependent.
            Additional arguments are passed in both the "initialize" and "detect changes" steps.
        :type additional_args: ``List[str]``
        """
        if not Path(path_to_before).is_file():
            raise ValueError(f"path_to_before {path_to_before} does not exist")

        if not Path(path_to_after).is_file():
            raise ValueError(f"path_to_after {path_to_after} does not exist")

        self._path_to_before = path_to_before
        self._path_to_after = path_to_after
        self._additional_args = additional_args

    def run_detector(self) -> None:
        """Runs the breaking change detector with the parameters given in ``__init__``. This method should populate
        the detector's internal data such that `is_breaking` and `lock_file_changed` do not require any additional
        invocations to the breaking change detector.
        """
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
        target = Path(temp_dir.name, f"{Path(self._path_to_before).stem}.proto")

        buf_args = [
            "--path",
            str(target.relative_to(
                Path(".").absolute())),  # buf requires relative pathing for roots...
            "--config",
            str(yaml_file_loc),
        ]
        if self._additional_args is not None:
            buf_args.extend(self._additional_args)

        copyfile(self._path_to_before, target)

        initial_code, initial_out, initial_err = run_command(
            ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_STATE_FILE)}", *buf_args]))
        initial_out, initial_err = ''.join(initial_out), ''.join(initial_err)

        if initial_code != 0 or len(initial_out) > 0 or len(initial_err) > 0:
            raise ChangeDetectorInitializeError(
                f"Unexpected error during init:\n\tExit Status Code: {initial_code}\n\tstdout: {initial_out}\n\t stderr: {initial_err}\n"
            )

        lock_location = Path(temp_dir.name, BUF_STATE_FILE)
        with open(lock_location, "r") as f:
            initial_lock = f.readlines()

        copyfile(self._path_to_after, target)

        final_code, final_out, final_err = run_command(
            ' '.join(
                [buf_path, f"breaking --against {Path(temp_dir.name, BUF_STATE_FILE)}", *buf_args]))
        final_out, final_err = ''.join(final_out), ''.join(final_err)

        # new with buf: lock must be manually re-built
        # but here we only re-build if there weren't any detected breaking changes
        if len(final_out) == len(final_err) == final_code == 0:
            _, _, _ = run_command(
                ' '.join([buf_path, f"build -o {Path(temp_dir.name, BUF_STATE_FILE)}", *buf_args]))
        with open(lock_location, "r") as f:
            final_lock = f.readlines()

        temp_dir.cleanup()

        self.initial_result = initial_code, initial_out, initial_err
        self.final_result = final_code, final_out, final_err
        self.initial_lock = initial_lock
        self.final_lock = final_lock

    def is_breaking(self) -> bool:
        """Returns whether the changes between the ``before`` and ``after`` states of the proto file given in ``__init__``
        violate any breaking change rules specified by this breaking change detector.

        :return: a boolean flag indicating if the changes between ``before`` and ``after`` are breaking
        :rtype: ``bool``
        """
        final_code, final_out, final_err = self.final_result

        # Ways buf output could be indicative of a breaking change:
        # 1) final_code (exit status code) is not 0 (e.g. it's 100)
        # 2) stdout/stderr is nonempty
        # 3) stdout/stderr contains "Failure"
        break_condition = lambda inp: len(inp) > 0 or bool(re.match(r"Failure", inp))
        return final_code != 0 or break_condition(final_out) or break_condition(final_err)

    def lock_file_changed(self) -> bool:
        """Returns whether the changes between the ``before`` and ``after`` states of the proto file given in ``__init__``
        cause a change in the detector's state file. This function is mostly used for testing, to ensure that the breaking change 
        detector is checking all of the protobuf features we are interested in.

        :return: a boolean flag indicating if the changes between ``before`` and ``after`` cause a change in the state file
        :rtype: ``bool``
        """
        return any(before != after for before, after in zip(self.initial_lock, self.final_lock))
