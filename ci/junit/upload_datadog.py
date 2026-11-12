#!/usr/bin/env python3
"""Upload JUnit XML collected by ci/junit/collect.py to Datadog CI Visibility.

collect.py groups its output by GitHub Actions workflow run, each with that run's own commit,
branch, PR and jobs in a run.json alongside the XML. This uploads each run's XML job by job with
`datadog-ci junit upload`, with DD_GIT_* set from that run's metadata, so Datadog attributes the
results to the run's actual commit rather than whatever happens to be checked out here (this
runs on a schedule, well after any of the runs it's uploading for).

Each upload is also tagged with what the run was (`envoy.trigger`: `pr` or `postsubmit`), the
branch it was against (`envoy.target_branch`) and the job the tests ran in (`envoy.job`, e.g.
`tsan`), as the same tests run in many jobs of many kinds of run.
"""

import argparse
import concurrent.futures
import datetime
import json
import os
import subprocess
import sys
import tempfile

DATADOG_CI = "@datadog/datadog-ci@5.25.0"
# A job can have thousands of XML files, and a single command line argument is capped at 128KiB
# (which is all of them, if the command goes through a shell, as it does with npx), so they are
# uploaded in batches whose paths fit well inside that.
MAX_BATCH_BYTES = 60 * 1024
# How many batches are uploaded at once. Each datadog-ci uploads 20 files at a time itself.
UPLOADS = 8


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


def install_datadog_ci():
    # Once, rather than have npx work out what to run for every batch.
    prefix = tempfile.mkdtemp(prefix="datadog-ci-")
    subprocess.run(
        ["npm", "install", "--silent", "--no-audit", "--no-fund", "--prefix", prefix, DATADOG_CI],
        check=True)
    return os.path.join(prefix, "node_modules", ".bin", "datadog-ci")


def job_dirs(run_dir, meta):
    # <run dir>/<job>/, with the job as collect.py recorded it, or just its directory name for
    # output collected before it did.
    jobs = meta.get("jobs", {})
    for name in sorted(os.listdir(run_dir)):
        job_dir = os.path.join(run_dir, name)
        if os.path.isdir(job_dir):
            yield job_dir, jobs.get(name, {"name": name, "leg": name})


def xml_files(job_dir):
    # datadog-ci's own directory/--auto-discovery scan silently skips anything whose filename
    # doesn't contain "test" (confirmed against datadog-ci 5.25.0), and collect.py names the
    # files after their test targets, which need not, so pass each file explicitly instead of
    # letting it discover them. Relative to the job's directory, to keep the paths short.
    return sorted(
        os.path.relpath(os.path.join(root, f), job_dir)
        for root, _, files in os.walk(job_dir)
        for f in files
        if f.endswith(".xml"))


def batches(files, max_bytes=MAX_BATCH_BYTES):
    batch, size = [], 0
    for path in files:
        if batch and size + len(path) + 1 > max_bytes:
            yield batch
            batch, size = [], 0
        batch.append(path)
        size += len(path) + 1
    if batch:
        yield batch


def run_env(repo, meta):
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
    return env


def run_tags(meta, job):
    tags = {
        "envoy.trigger": meta.get("trigger", ""),
        "envoy.target_branch": meta.get("target_branch", ""),
        "envoy.job": job.get("leg", ""),
    }
    return [arg for key, value in tags.items() if value for arg in ("--tags", f"{key}:{value}")]


def upload(datadog_ci, repo, meta, run_dir, service, dry_run=False):
    # True if all of the run's XML was uploaded, and how many files that was.
    env = run_env(repo, meta)
    print(f"  sha {meta.get('sha', '')[:12] or '?'}, branch {meta.get('branch', '?')}, "
          f"{meta.get('trigger', '?')} against {meta.get('target_branch', '?')}")
    uploads = []
    for job_dir, job in job_dirs(run_dir, meta):
        files = xml_files(job_dir)
        if not files:
            continue
        print(f"    {job.get('leg', '?')}: {len(files)} files")
        uploads += [(job_dir, job, batch) for batch in batches(files)]

    def run(job_dir, job, batch):
        command = [
            datadog_ci, "junit", "upload", "--service", service,
            *run_tags(meta, job), *(["--dry-run"] if dry_run else []), *batch]
        # Its output is a line for every file, so only shown if it fails.
        result = subprocess.run(command, env=env, cwd=job_dir, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"    {job.get('leg', '?')}: datadog-ci exited {result.returncode} "
                  f"uploading {len(batch)} files\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
        return result.returncode == 0

    with concurrent.futures.ThreadPoolExecutor(UPLOADS) as pool:
        results = list(pool.map(lambda u: run(*u), uploads))
    return all(results), sum(len(batch) for (_, _, batch), ok in zip(uploads, results) if ok)


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
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Run datadog-ci with --dry-run, and leave the watermark as it is.")
    args = parser.parse_args()

    if not os.environ.get("DD_API_KEY"):
        print("DD_API_KEY not set, skipping Datadog upload.")
        return 0

    watermark = load_watermark(args.watermark) if args.watermark else {}
    datadog_ci = None
    uploaded = failed = skipped = already = 0
    for run_dir in run_dirs(args.junit_dir):
        run_id = os.path.basename(run_dir)
        if run_id in watermark:
            already += 1
            continue
        with open(os.path.join(run_dir, "run.json")) as f:
            meta = json.load(f)
        print(f"  {run_dir}:")
        datadog_ci = datadog_ci or install_datadog_ci()
        ok, files = upload(datadog_ci, args.repo, meta, run_dir, args.service, args.dry_run)
        if not ok:
            failed += 1
            print(f"  {run_dir}: upload failed")
        elif not files:
            skipped += 1
        else:
            uploaded += 1
            watermark[run_id] = meta.get("created_at", "")

    if args.watermark and not args.dry_run:
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
