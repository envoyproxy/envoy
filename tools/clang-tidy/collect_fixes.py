#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect clang-tidy YAML fix files for specific Bazel targets and merge them into "
            "a single clang-tidy-fixes.yaml file at the workspace root."
        )
    )
    parser.add_argument(
        "--workspace_root",
        type=Path,
        default=Path.cwd(),
        help="Workspace root where clang-tidy-fixes.yaml will be written.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional explicit output path. Defaults to <workspace_root>/clang-tidy-fixes.yaml.",
    )
    parser.add_argument(
        "--repository",
        help="Bazel repository name for the current workspace, for example 'envoy'.",
    )
    parser.add_argument(
        "targets",
        nargs="+",
        help=(
            "Bazel targets or target patterns to collect, for example "
            "//source/common/stats:symbol_table_lib or //source/common/..."
        ),
    )
    return parser.parse_args()


def get_bazel_startup_options() -> list[str]:
    return shlex.split(os.environ.get("BAZEL_STARTUP_OPTION_LIST", ""))


def get_bazel_build_options() -> list[str]:
    return shlex.split(os.environ.get("BAZEL_BUILD_OPTION_LIST", ""))


def get_output_base(workspace_root: Path) -> Path:
    return Path(
        subprocess.check_output(
            [
                "bazel",
                *get_bazel_startup_options(),
                "info",
                *get_bazel_build_options(),
                "output_base",
            ],
            text=True,
            cwd=workspace_root,
        ).strip()
    )


def search_roots(workspace_root: Path) -> list[Path]:
    # Clang-tidy aspect outputs are materialized under the Bazel execroot rather than the
    # workspace symlink tree. For Envoy clang-tidy runs we only need the fastbuild bin tree.
    execroot = get_output_base(workspace_root) / "execroot" / workspace_root.name
    return [execroot / "bazel-out" / "k8-fastbuild" / "bin"]


def parse_target(target: str) -> tuple[str | None, str, str | None, bool]:
    repository = None
    if target.startswith("@"):
        repository, separator, target = target[1:].partition("//")
        if not separator:
            raise ValueError(f"Unsupported Bazel target: {target}")
        target = f"//{target}"

    if not target.startswith("//"):
        raise ValueError(f"Unsupported Bazel target: {target}")

    label = target[2:]
    if label == "...":
        return repository, "", None, True

    if label.endswith("/..."):
        return repository, label[:-4], None, True

    if ":" in label:
        package_path, target_name = label.split(":", 1)
        if target_name in ("all", "*"):
            return repository, package_path, None, False
        return repository, package_path, target_name, False

    return repository, label, None, False


def target_patterns(target: str, repository_name: str | None) -> list[str]:
    repository, package_path, target_name, recursive = parse_target(target)
    escaped_package_path = package_path.strip("/")
    target_root = escaped_package_path
    tidy_root = escaped_package_path

    if repository is not None and repository != repository_name:
        # External targets such as @envoy_api//bazel/foo are emitted under:
        #   external/envoy_api/bazel/foo/bazel_clang_tidy_external/envoy_api/bazel/foo/
        target_root = f"external/{repository}/{escaped_package_path}" if escaped_package_path else f"external/{repository}"
        tidy_root = target_root

    if recursive:
        # //source/common/... -> source/common/**/bazel_clang_tidy_*/**/*.clang-tidy.yaml
        # This keeps the search inside the requested subtree while still allowing nested packages.
        prefix = f"{target_root}/" if target_root else ""
        return [f"{prefix}**/bazel_clang_tidy_*/**/*.clang-tidy.yaml"]

    # A package such as //source/common/stats is emitted under:
    #   source/common/stats/bazel_clang_tidy_source/common/stats/
    # The aspect mirrors the package path after the bazel_clang_tidy_ prefix.
    # External packages use the same convention, but rooted under external/<repo>/...
    tidy_dir = (
        f"{target_root}/bazel_clang_tidy_{tidy_root}"
        if target_root
        else "bazel_clang_tidy"
    )
    if target_name is None:
        # //pkg or //pkg:all -> collect every clang-tidy YAML produced for that package.
        return [f"{tidy_dir}/**/*.clang-tidy.yaml"]

    # //pkg:lib -> only YAML files emitted for that target, e.g.
    #   foo.cc.lib.clang-tidy.yaml
    return [f"{tidy_dir}/**/*.{target_name}.clang-tidy.yaml"]


def collect_fix_files(
    roots: list[Path], targets: list[str], repository_name: str | None
) -> list[Path]:
    seen: set[Path] = set()
    results: list[Path] = []

    for root in roots:
        if not root.exists():
            continue
        for target in targets:
            for pattern in target_patterns(target, repository_name):
                for candidate in sorted(root.glob(pattern)):
                    resolved = candidate.resolve()
                    if resolved in seen:
                        continue
                    seen.add(resolved)
                    results.append(candidate)
    return results


def merge_file_contents(fix_files: list[Path]) -> str:
    merged_contents = []
    for fix_file in fix_files:
        content = fix_file.read_text(encoding="utf-8").strip()
        if content:
            merged_contents.append(content)

    return "\n---\n".join(merged_contents) + "\n"


def main() -> int:
    args = parse_args()
    workspace_root = args.workspace_root.resolve()
    output = args.output.resolve() if args.output else workspace_root / "clang-tidy-fixes.yaml"

    try:
        roots = search_roots(workspace_root)
        fix_files = collect_fix_files(roots, args.targets, args.repository)
    except subprocess.CalledProcessError as error:
        print(f"Failed to query Bazel output_base: {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1

    if not fix_files:
        searched = ", ".join(str(root) for root in roots)
        requested = ", ".join(args.targets)
        print(
            f"No clang-tidy YAML files found for targets [{requested}] under: {searched}",
            file=sys.stderr,
        )
        return 1

    merged = merge_file_contents(fix_files)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(merged, encoding="utf-8")

    print(f"Wrote {output} from {len(fix_files)} clang-tidy YAML files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
