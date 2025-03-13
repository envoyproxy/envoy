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

from pathlib import Path
from typing import List

from tools.api_proto_breaking_change_detector.buf_utils import check_breaking, pull_buf_deps
from tools.api_proto_breaking_change_detector.detector_errors import ChangeDetectorError


class ProtoBreakingChangeDetector(object):
    """Abstract breaking change detector interface"""

    def run_detector(self) -> None:
        """Run the breaking change detector to detect rule violations

        This method should populate the detector's internal data such
        that `is_breaking` does not require any additional invocations
        to the breaking change detector.
        """
        pass

    def is_breaking(self) -> bool:
        """Return True if breaking changes were detected in the given protos"""
        pass

    def get_breaking_changes(self) -> List[str]:
        """Return a list of strings containing breaking changes output by the tool"""
        pass


class BufWrapper(ProtoBreakingChangeDetector):
    """Breaking change detector implemented with buf"""

    def __init__(
            self,
            path_to_changed_dir: str,
            git_ref: str,
            git_path: str,
            subdir: str = None,
            buf_path: str = None,
            config_file_loc: str = None,
            additional_args: List[str] = None) -> None:
        """Initialize the configuration of buf

        This function sets up any necessary config without actually
        running buf against any proto files.

        BufWrapper takes a path to a directory containing proto files
        as input, and it checks if these proto files break any changes
        from a given initial state.

        The initial state is input as a git ref. The constructor expects
        a git ref string, as well as an absolute path to a .git folder
        for the repository.

        Args:
            path_to_changed_dir {str} -- absolute path to a directory containing proto files in the after state
            buf_path {str} -- path to the buf binary (default: "buf")
            git_ref {str} -- git reference to use for the initial state of the protos (typically a commit hash)
            git_path {str} -- absolute path to .git folder for the repository of interest

            subdir {str} -- subdirectory within git repository from which to search for .proto files (default: None, e.g. stay in root)
            additional_args {List[str]} -- additional arguments passed into the buf binary invocations
            config_file_loc {str} -- absolute path to buf.yaml configuration file (if not provided, uses default buf configuration)
        """
        if not Path(path_to_changed_dir).is_dir():
            raise ValueError(f"path_to_changed_dir {path_to_changed_dir} is not a valid directory")

        if Path.cwd() not in Path(path_to_changed_dir).parents:
            raise ValueError(
                f"path_to_changed_dir {path_to_changed_dir} must be a subdirectory of the cwd ({Path.cwd()})"
            )

        if not Path(git_path).exists():
            raise ChangeDetectorError(f'path to .git folder {git_path} does not exist')

        self._path_to_changed_dir = path_to_changed_dir
        self._additional_args = additional_args
        self._buf_path = buf_path or "buf"
        self._config_file_loc = config_file_loc
        self._git_ref = git_ref
        self._git_path = git_path
        self._subdir = subdir
        self._final_result = None

        pull_buf_deps(
            self._buf_path,
            self._path_to_changed_dir,
            config_file_loc=self._config_file_loc,
            additional_args=self._additional_args)

    def run_detector(self) -> None:
        self._final_result = check_breaking(
            self._buf_path,
            self._path_to_changed_dir,
            git_ref=self._git_ref,
            git_path=self._git_path,
            subdir=self._subdir,
            config_file_loc=self._config_file_loc,
            additional_args=self._additional_args)

    def is_breaking(self) -> bool:
        if not self._final_result:
            raise ChangeDetectorError("Must invoke run_detector() before checking if is_breaking()")

        final_code, final_out, final_err = self._final_result
        final_out, final_err = '\n'.join(final_out), '\n'.join(final_err)

        if final_err != "":
            raise ChangeDetectorError(f"Error from buf: {final_err}")

        if final_code != 0:
            return True
        if final_out != "":
            return True
        return False

    def get_breaking_changes(self) -> List[str]:
        _, final_out, _ = self._final_result
        return filter(lambda x: len(x) > 0, final_out) if self.is_breaking() else []
