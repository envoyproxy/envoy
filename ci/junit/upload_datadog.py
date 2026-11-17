#!/usr/bin/env python3
"""Upload JUnit XML collected by ci/junit/collect.py to Datadog CI Visibility.

collect.py groups its output by GitHub Actions workflow run, each with that run's own commit,
branch and PR in a run.json alongside the XML. This uploads each run's XML in one
`datadog-ci junit upload` call, with DD_GIT_* set from that run's metadata, so Datadog attributes
the results to the run's actual commit rather than whatever happens to be checked out here (this
runs on a schedule, well after any of the runs it's uploading for).
"""

import argparse
import datetime
import json
import os
import subprocess
import sys

DATADOG_CI = "@datadog/datadog-ci@5.25.0"


def run_dirs(junit_dir):
    # <junit_dir>/<workflow>/<run id>/, each with a run.json if collect.py found anything there.
    if not os.path.isdir(junit_dir):
        return
    for workflow in sorted(os.listdir(junit_dir)):
        workflow_dir = os.path.join(junit_dir, workflow)
        if not os.path.isdir(workflow_dir):
            continue
        for run_id in sorted(os.listdir(workflow_dir)):
            run_dir = os.path.join(workflow_dir, run_id)
            if os.path.isfile(os.path.join(run_dir, "run.json")):
                yield run_dir


def load_watermark(path):
    # {run id: created_at}, run ids already uploaded by a previous invocation of this script -
    # this one runs on a schedule with a sliding time window, with no other memory of what a
    # prior run already covered, so an overlapping window (the cron firing early, or a wider
    # manual --since-hours) would otherwise re-upload the same run's results every time it
    # resurfaces. A single highest-run-id watermark can't do this safely: runs don't complete in
    # id order (a slow run can finish after a newer, faster one already bumped it forward), so
    # this tracks the actual set rather than a cutoff.
    try:
        with open(path) as f:
            return json.load(f).get("uploaded", {})
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_watermark(path, uploaded, retention_hours):
    cutoff = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(hours=retention_hours)
    pruned = {}
    for run_id, created_at in uploaded.items():
        try:
            ts = datetime.datetime.strptime(created_at, "%Y-%m-%dT%H:%M:%SZ")
        except ValueError:
            continue  # unparseable - drop it rather than keep it forever
        if ts.replace(tzinfo=datetime.timezone.utc) >= cutoff:
            pruned[run_id] = created_at
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as f:
        json.dump({"uploaded": pruned}, f)


def xml_files(run_dir):
    # collect.py names files <hash[:12]>.xml, with no "test" in the name - datadog-ci's own
    # directory/--auto-discovery scan silently skips anything whose filename doesn't contain
    # "test" (confirmed against datadog-ci 5.25.0), so pass each file explicitly instead of
    # letting it discover them.
    return [
        os.path.join(root, f)
        for root, _, files in os.walk(run_dir)
        for f in files
        if f.endswith(".xml")
    ]


def upload(repo, meta, files, service):
    env = dict(os.environ)
    env.update({
        "DD_GIT_REPOSITORY_URL": f"https://github.com/{repo}",
        "DD_GIT_COMMIT_SHA": meta.get("sha", ""),
        "DD_GIT_BRANCH": meta.get("branch", ""),
        "DD_GIT_PULL_REQUEST_BASE_BRANCH": meta.get("base_branch", ""),
        "DD_GIT_PULL_REQUEST_BASE_BRANCH_SHA": meta.get("base_sha", ""),
    })
    if meta.get("pr"):
        env["DD_PR_NUMBER"] = str(meta["pr"])
    print(f"  sha {meta.get('sha', '')[:12] or '?'}, branch {meta.get('branch', '?')}, "
          f"{len(files)} files")
    result = subprocess.run(
        ["npx", "--yes", DATADOG_CI, "junit", "upload", "--service", service, *files],
        env=env)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--service", default="envoy-ci")
    parser.add_argument("--junit-dir", default="junit")
    parser.add_argument(
        "--watermark", default="",
        help="Path to a run-id watermark file, to skip runs a previous invocation already "
             "uploaded (e.g. from an overlapping --since-hours window). Skipped if unset.")
    parser.add_argument("--watermark-retention-hours", type=float, default=48)
    args = parser.parse_args()

    if not os.environ.get("DD_API_KEY"):
        print("DD_API_KEY not set, skipping Datadog upload.")
        return 0

    watermark = load_watermark(args.watermark) if args.watermark else {}
    uploaded = failed = skipped = already = 0
    for run_dir in run_dirs(args.junit_dir):
        run_id = os.path.basename(run_dir)
        if run_id in watermark:
            already += 1
            continue
        files = xml_files(run_dir)
        if not files:
            skipped += 1
            continue
        with open(os.path.join(run_dir, "run.json")) as f:
            meta = json.load(f)
        print(f"  {run_dir}:")
        if upload(args.repo, meta, files, args.service):
            uploaded += 1
            watermark[run_id] = meta.get("created_at", "")
        else:
            failed += 1
            print(f"  {run_dir}: upload failed")

    if args.watermark:
        save_watermark(args.watermark, watermark, args.watermark_retention_hours)

    summary = (f"uploaded: {uploaded}, failed: {failed}, skipped (no xml): {skipped}, "
               f"already uploaded: {already}")
    print(summary)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write(f"### Uploaded to Datadog\n\n{summary}\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
