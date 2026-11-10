#!/usr/bin/env python3
"""Collect JUnit XML for recent CI runs from EngFlow, without slowing the runs down.

CI builds with `--remote_download_minimal`, so test.xml files never reach the runner. They are
in EngFlow though: for each completed run of the selected workflows, this finds the Bazel
invocations its jobs streamed to EngFlow, the tests those invocations actually executed (cached
results are skipped, which is what flake tracking wants), and downloads each test's test.xml.

The EngFlow calls are the ones its UI uses (no auth needed on this cluster), so treat them as
best effort: anything that fails is logged and skipped.

Output is grouped by workflow run (<out>/<workflow>/<run id>/...), alongside a run.json of that
run's git metadata (commit, branch, PR), since each run is its own commit and whatever uploads
this on needs that rather than whatever happens to be checked out when it runs.
"""

import argparse
import concurrent.futures
import datetime
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

ENGFLOW = "https://mordenite.cluster.engflow.com"
GITHUB_API = "https://api.github.com"
INVOCATION_RE = re.compile(rb"mordenite\.cluster\.engflow\.com/invocation/([0-9a-f-]{36})")
# The GetTarget response also lists the test binary and logs; test.xml files are far smaller.
MAX_XML_BYTES = 20 * 1024 * 1024


class _NoAuthRedirect(urllib.request.HTTPRedirectHandler):
    # Job logs redirect to blob storage, which rejects the GitHub token.
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None:
            new.headers.pop("Authorization", None)
            new.unredirected_hdrs.pop("Authorization", None)
        return new


_opener = urllib.request.build_opener(_NoAuthRedirect)


def _get(url, token=None, timeout=300):
    req = urllib.request.Request(url)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
        req.add_header("Accept", "application/vnd.github+json")
    with _opener.open(req, timeout=timeout) as r:
        return r.read()


def _github(path, token, key):
    items, page = [], 1
    while True:
        sep = "&" if "?" in path else "?"
        data = json.loads(_get(f"{GITHUB_API}{path}{sep}per_page=100&page={page}", token))
        items += data[key]
        if len(data[key]) < 100:
            return items
        page += 1


def _varint(b, i):
    v = s = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7f) << s
        s += 7
        if not c & 0x80:
            return v, i


def _get_target(invocation, label):
    # gRPC-web, which EngFlow only serves over HTTP/2, hence curl.
    def field(n, s):
        s = s.encode()
        return bytes([n << 3 | 2, len(s)]) + s
    body = field(1, "default") + field(2, invocation) + field(3, label)
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(b"\x00" + struct.pack(">I", len(body)) + body)
        req = f.name
    try:
        return subprocess.run(
            ["curl", "-sf", "--http2", "--max-time", "60", "-X", "POST",
             "-H", "Content-Type: application/grpc-web+proto", "-H", "X-Grpc-Web: 1",
             "--data-binary", f"@{req}",
             f"{ENGFLOW}/engflow.resultstore.v1alpha.ResultStore/GetTarget"],
            capture_output=True, check=True).stdout
    finally:
        os.unlink(req)


def _digests(data):
    # Outputs are listed as compact digests: `0a 20` + raw sha256, then `10` + varint size.
    seen = set()
    for j in range(len(data) - 35):
        if data[j] == 0x0a and data[j + 1] == 0x20 and data[j + 34] == 0x10:
            size, _ = _varint(data, j + 35)
            digest = (data[j + 2:j + 34].hex(), size)
            if digest not in seen:
                seen.add(digest)
                yield digest


def executed_tests(invocation):
    trace = json.loads(_get(f"{ENGFLOW}/api/profiling/v1/instances/default/invocations/{invocation}"))
    labels = set()
    for event in trace.get("traceEvents", []):
        parts = event.get("name", "").split()
        if event.get("cat") == "worker_time" and len(parts) > 2 and parts[1] == "TestRunner":
            labels.add(parts[0])
    return sorted(labels)


def test_xmls(invocation, label):
    for hash_, size in _digests(_get_target(invocation, label)):
        if size > MAX_XML_BYTES:
            continue
        try:
            blob = _get(f"{ENGFLOW}/api/contentaddressablestorage/v1/instances/default/blobs/{hash_}/{size}")
        except urllib.error.URLError:
            continue
        if blob.lstrip().startswith(b"<?xml"):
            yield hash_, blob


def _safe(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("_")


def run_dir(out, run):
    return os.path.join(out, _safe(run["name"]), str(run["id"]))


def run_meta(run):
    # Each run is its own commit/branch/PR, so whatever uploads this on needs it alongside the
    # XML rather than relying on whatever happens to be checked out when it runs.
    pr = (run.get("pull_requests") or [{}])[0]
    base = pr.get("base") or {}
    return {
        "sha": run.get("head_sha", ""),
        "branch": run.get("head_branch", ""),
        "pr": pr.get("number", ""),
        "base_branch": base.get("ref", ""),
        "base_sha": base.get("sha", ""),
        "event": run.get("event", ""),
        "url": run.get("html_url", ""),
        # Lets a watermark prune old entries by age without another GitHub API call.
        "created_at": run.get("created_at", ""),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--workflows", default="Envoy/Checks,Envoy/Prechecks")
    parser.add_argument("--since-hours", type=float, default=2)
    parser.add_argument("--out", default="junit")
    parser.add_argument("--workers", type=int, default=16)
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN")
    workflows = {w.strip() for w in args.workflows.split(",") if w.strip()}
    since = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(hours=args.since_hours)

    runs = [
        run for run in _github(
            f"/repos/{args.repo}/actions/runs?status=completed&created=>={since:%Y-%m-%dT%H:%M:%SZ}",
            token, "workflow_runs")
        if run["name"] in workflows and run["conclusion"] not in ("skipped", "cancelled")
    ]
    print(f"{len(runs)} completed runs of {sorted(workflows)} since {since:%Y-%m-%d %H:%M}Z")

    jobs = [
        (run, job)
        for run in runs
        for job in _github(f"/repos/{args.repo}/actions/runs/{run['id']}/jobs", token, "jobs")
        if job.get("conclusion") not in (None, "skipped")
    ]
    totals = {"jobs": 0, "invocations": 0, "tests": 0, "xml": 0, "missing": 0}

    def find_tests(run, job):
        # Each job's invocations, and the tests each of them actually executed.
        try:
            log = _get(f"{GITHUB_API}/repos/{args.repo}/actions/jobs/{job['id']}/logs", token)
        except urllib.error.URLError as e:
            print(f"  {job['name']}: no log ({e})")
            return []
        found = []
        for invocation in dict.fromkeys(m.decode() for m in INVOCATION_RE.findall(log)):
            try:
                labels = executed_tests(invocation)
            except (urllib.error.URLError, ValueError) as e:
                print(f"  {job['name']} {invocation}: no trace ({e})")
                continue
            if labels:
                print(f"  {run['name']} {run['id']} {job['name']}: {invocation} {len(labels)} executed tests")
                found.append((invocation, labels))
        return found

    def fetch_xml(job_dir, invocation, label):
        written = 0
        try:
            for hash_, blob in test_xmls(invocation, label):
                path = os.path.join(job_dir, invocation, _safe(label.lstrip("/")), f"{hash_[:12]}.xml")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(blob)
                written += 1
        except (subprocess.CalledProcessError, urllib.error.URLError) as e:
            print(f"  {invocation} {label}: {e}")
        return written

    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        tests = []
        meta_written = set()
        for (run, job), found in zip(jobs, pool.map(lambda rj: find_tests(*rj), jobs)):
            if found:
                totals["jobs"] += 1
                if run["id"] not in meta_written:
                    meta_written.add(run["id"])
                    os.makedirs(run_dir(args.out, run), exist_ok=True)
                    with open(os.path.join(run_dir(args.out, run), "run.json"), "w") as f:
                        json.dump(run_meta(run), f)
            job_dir = os.path.join(run_dir(args.out, run), _safe(job["name"]))
            for invocation, labels in found:
                totals["invocations"] += 1
                tests += [(job_dir, invocation, label) for label in labels]
        for written in pool.map(lambda t: fetch_xml(*t), tests):
            totals["tests"] += 1
            totals["xml"] += written
            totals["missing"] += not written

    summary = ", ".join(f"{k}: {v}" for k, v in totals.items())
    print(summary)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write(f"### JUnit collected from EngFlow\n\n{len(runs)} runs; {summary}\n")


if __name__ == "__main__":
    sys.exit(main())
