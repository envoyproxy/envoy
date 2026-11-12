#!/usr/bin/env python3
"""Collect JUnit XML for recent CI runs from EngFlow, without slowing the runs down.

CI builds with `--remote_download_minimal`, so test.xml files never reach the runner. They are
in EngFlow though: for each completed run of the selected workflows, this finds the Bazel
invocations its jobs streamed to EngFlow, the tests those invocations actually executed (cached
results are skipped, which is what flake tracking wants), and downloads each test's test.xml.

The EngFlow calls are the ones its UI uses (no auth needed on this cluster), so treat them as
best effort: anything that fails is logged and skipped.

There is a call per executed test to find its test.xml, and there can be thousands of tests in
a run, so the calls are spread over a few connections that are kept open, and the XML itself is
read in batches.

Output is grouped by workflow run (<out>/<workflow>/<run id>/...), alongside a run.json of that
run's git metadata (commit, branch, PR) and its jobs, since each run is its own commit and
whatever uploads this on needs that rather than whatever happens to be checked out when it runs.

The CI workflows are triggered by `workflow_run`, so the head commit and branch GitHub records
for a run are those of `main` at the time, whatever the run was testing. What it was testing is
in its job names, e.g. `Check (pr/1234/main@abc1234) / tsan / ...` or
`Precheck (postsubmit/release/v1.40@abc1234) / ...`, and the metadata is taken from there.
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
import time
import urllib.error
import urllib.parse
import urllib.request

ENGFLOW = "https://mordenite.cluster.engflow.com"
GITHUB_API = "https://api.github.com"
INVOCATION_RE = re.compile(rb"mordenite\.cluster\.engflow\.com/invocation/([0-9a-f-]{36})")
# The GetTarget response also lists the test binary and logs; test.xml files are far smaller.
MAX_XML_BYTES = 20 * 1024 * 1024
# `<kind> (pr/<number>/<target branch>@<sha>)` or `<kind> (postsubmit/<branch>@<sha>)`.
JOB_REF_RE = re.compile(
    r"\((?:pr/(?P<pr>\d+)|postsubmit)/(?P<branch>[^@)]+)@(?P<sha>[0-9a-f]{7,40})\)")
# The server's limit for a batch read is 10MB. Blobs are batched up to this, and anything bigger
# than a batch is read on its own.
MAX_BATCH_READ_BYTES = 8 * 1024 * 1024
MAX_BATCH_READ_BLOBS = 200
# How many connections the EngFlow calls are spread over.
CURL_CONNECTIONS = 32
# How many tests' XML is fetched and written at a time, so that a big window's is never all held.
FETCH_TESTS = 5000
# How long to wait out a GitHub rate limit, at most, before giving up on a request.
MAX_RATE_LIMIT_WAIT = 900
# GitHub returns at most 1000 runs for a filtered listing, so list the window in slices.
LIST_SLICE = datetime.timedelta(hours=1)


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
    for attempt in range(4):
        try:
            with _opener.open(req, timeout=timeout) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            # A big window is thousands of GitHub requests, and the token's limit is shared
            # with every other workflow in the repo, so wait a limit out rather than drop data.
            limited = e.code == 429 or (
                e.code == 403 and e.headers.get("x-ratelimit-remaining") == "0")
            if not token or not limited or attempt == 3:
                raise
            wait = int(e.headers.get("retry-after") or 0)
            if not wait and e.headers.get("x-ratelimit-reset"):
                wait = int(e.headers["x-ratelimit-reset"]) - int(time.time()) + 5
            wait = max(10, min(wait or 60, MAX_RATE_LIMIT_WAIT))
            print(f"  GitHub rate limit, waiting {wait}s")
            time.sleep(wait)


def rate_limit(token):
    # What is left of the token's GitHub API budget, which is shared with the repo's other
    # workflows.
    try:
        core = json.loads(_get(f"{GITHUB_API}/rate_limit", token))["resources"]["core"]
        return f"{core['remaining']} of {core['limit']} requests left"
    except (urllib.error.URLError, ValueError, KeyError) as e:
        return f"unknown ({e})"


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


def _enc_varint(value):
    out = b""
    while True:
        byte = value & 0x7f
        value >>= 7
        out += bytes([byte | (0x80 if value else 0)])
        if not value:
            return out


def _field(number, value):
    # A length-delimited field, or a varint one for an int.
    if isinstance(value, int):
        return _enc_varint(number << 3) + _enc_varint(value)
    value = value.encode() if isinstance(value, str) else value
    return _enc_varint(number << 3 | 2) + _enc_varint(len(value)) + value


def _fields(data):
    # (number, value) for each field of a message, with nested messages left as bytes.
    i = 0
    while i < len(data):
        key, i = _varint(data, i)
        wire = key & 7
        if wire == 0:
            value, i = _varint(data, i)
        elif wire == 2:
            size, i = _varint(data, i)
            value = data[i:i + size]
            i += size
        elif wire == 1:
            value = data[i:i + 8]
            i += 8
        elif wire == 5:
            value = data[i:i + 4]
            i += 4
        else:
            return
        yield key >> 3, value


def _grpc_web_messages(response):
    # The messages of a gRPC-web response, without its trailers frame.
    i = 0
    while i + 5 <= len(response):
        size = struct.unpack(">I", response[i + 1:i + 5])[0]
        if not response[i] & 0x80:
            yield response[i + 5:i + 5 + size]
        i += 5 + size


def _grpc_web_many(method, bodies):
    # gRPC-web, which EngFlow only serves over HTTP/2, hence curl. There can be thousands of
    # calls, and a process and a TLS handshake for each is most of what they would cost, so they
    # are split between a few curl processes, each of which makes its share one after another on
    # one connection. The responses are in the order of the requests, with b"" for any that
    # failed.
    responses = [b""] * len(bodies)
    with tempfile.TemporaryDirectory() as tmp:
        def run(share):
            config = []
            for n in range(share, len(bodies), CURL_CONNECTIONS):
                with open(os.path.join(tmp, f"{n}.req"), "wb") as f:
                    f.write(b"\x00" + struct.pack(">I", len(bodies[n])) + bodies[n])
                config += [
                    f'url = "{ENGFLOW}/{method}"',
                    'header = "Content-Type: application/grpc-web+proto"',
                    'header = "X-Grpc-Web: 1"',
                    f'data-binary = "@{os.path.join(tmp, f"{n}.req")}"',
                    f'output = "{os.path.join(tmp, f"{n}.resp")}"',
                    "http2",
                    "fail",
                    "max-time = 120",
                    "retry = 2",
                    "next",
                ]
            if not config:
                return
            path = os.path.join(tmp, f"config.{share}")
            with open(path, "w") as f:
                f.write("\n".join(config) + "\n")
            subprocess.run(["curl", "-s", "-K", path], capture_output=True)

        with concurrent.futures.ThreadPoolExecutor(CURL_CONNECTIONS) as pool:
            list(pool.map(run, range(CURL_CONNECTIONS)))
        for n in range(len(bodies)):
            try:
                with open(os.path.join(tmp, f"{n}.resp"), "rb") as f:
                    responses[n] = f.read()
            except FileNotFoundError:
                pass
    return responses


def get_targets(tests):
    # The GetTarget response for each (invocation, label).
    return _grpc_web_many(
        "engflow.resultstore.v1alpha.ResultStore/GetTarget",
        [_field(1, "default") + _field(2, invocation) + _field(3, label)
         for invocation, label in tests])


def read_blobs(digests):
    # {(hash, size): contents} for as many of the digests as could be read.
    batches, batch, size = [], [], 0
    single = []
    for digest in sorted(set(digests)):
        if digest[1] > MAX_BATCH_READ_BYTES:
            single.append(digest)
            continue
        full = size + digest[1] > MAX_BATCH_READ_BYTES or len(batch) >= MAX_BATCH_READ_BLOBS
        if batch and full:
            batches.append(batch)
            batch, size = [], 0
        batch.append(digest)
        size += digest[1]
    if batch:
        batches.append(batch)
    blobs = {}
    responses = _grpc_web_many(
        "build.bazel.remote.execution.v2.ContentAddressableStorage/BatchReadBlobs",
        [_field(1, "default") + b"".join(
            _field(2, _field(1, hash_) + _field(2, size)) for hash_, size in batch)
         for batch in batches])
    for response in responses:
        for message in _grpc_web_messages(response):
            for number, item in _fields(message):
                if number != 1:
                    continue
                item = dict(_fields(item))
                digest = dict(_fields(item.get(1, b"")))
                if 2 in item and 1 in digest:
                    blobs[(digest[1].decode(), digest.get(2, 0))] = item[2]
    # Anything too big for a batch, or that a batch did not return.
    for digest in single + [d for batch in batches for d in batch if d not in blobs]:
        try:
            blobs[digest] = _get(
                f"{ENGFLOW}/api/contentaddressablestorage/v1/instances/default/blobs/"
                f"{digest[0]}/{digest[1]}")
        except urllib.error.URLError:
            continue
    return blobs


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


def _safe(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("_")


def run_dir(out, run):
    return os.path.join(out, _safe(run["name"]), str(run["id"]))


def job_ref(job_names):
    # What a run was testing, from the first of its job names that says.
    for name in job_names:
        m = JOB_REF_RE.search(name)
        if m:
            return m.groupdict()
    return None


def job_leg(name):
    # `Check (postsubmit/main@abc1234) / tsan / ./ci/do_ci.sh tsan` -> `tsan`.
    parts = [part.strip() for part in name.split(" / ")]
    return " / ".join(parts[1:-1]) if len(parts) > 2 else parts[-1]


def run_meta(repo, run, job_names, token):
    # Each run is its own commit/branch/PR, so whatever uploads this on needs it alongside the
    # XML rather than relying on whatever happens to be checked out when it runs.
    meta = {
        "sha": run.get("head_sha", ""),
        "branch": run.get("head_branch", ""),
        "target_branch": run.get("head_branch", ""),
        "trigger": run.get("event", ""),
        "pr": "",
        "base_branch": "",
        "base_sha": "",
        "event": run.get("event", ""),
        "url": run.get("html_url", ""),
        # The run itself, for the results to link back to.
        "run_id": run.get("id", ""),
        "run_number": run.get("run_number", ""),
        "run_attempt": run.get("run_attempt", ""),
        "workflow": run.get("name", ""),
        # Lets a watermark prune old entries by age without another GitHub API call.
        "created_at": run.get("created_at", ""),
    }
    ref = job_ref(job_names)
    if not ref:
        return meta
    meta.update({"sha": ref["sha"], "branch": ref["branch"], "target_branch": ref["branch"]})
    if not ref["pr"]:
        meta["trigger"] = "postsubmit"
        # The job names only have the abbreviated commit, which GitHub will not look up if any
        # fork has another that starts the same, so find it on the branch, where it was the
        # newest commit or near it when the run was created.
        try:
            commits = json.loads(_get(
                f"{GITHUB_API}/repos/{repo}/commits?sha={urllib.parse.quote(ref['branch'])}"
                f"&until={run.get('created_at', '')}&per_page=100", token))
            full = [c["sha"] for c in commits if c["sha"].startswith(ref["sha"])]
            if full:
                meta["sha"] = full[0]
            else:
                print(f"  {run['id']}: could not resolve commit {ref['sha']} on {ref['branch']}")
        except (urllib.error.URLError, ValueError, KeyError) as e:
            print(f"  {run['id']}: could not resolve commit {ref['sha']} ({e})")
        return meta
    meta.update({"trigger": "pr", "pr": int(ref["pr"]), "base_branch": ref["branch"]})
    # Not known from the job names. A fork's branch can have any name, `main` included, so the
    # fallback is one that cannot be mistaken for a branch of this repo.
    meta["branch"] = f"pr/{ref['pr']}"
    try:
        pull = json.loads(_get(f"{GITHUB_API}/repos/{repo}/pulls/{ref['pr']}", token))
        head, base = pull["head"], pull["base"]
        same_repo = (head.get("repo") or {}).get("full_name") == repo
        meta["branch"] = head["ref"] if same_repo else head["label"]
        meta["base_sha"] = base["sha"]
        # An abbreviated commit from a fork cannot be looked up in this repo, so find it among
        # the pull request's own, starting with the one it is most likely to be.
        shas = [head["sha"]]
        if not head["sha"].startswith(ref["sha"]):
            shas += [
                commit["sha"] for commit in json.loads(_get(
                    f"{GITHUB_API}/repos/{repo}/pulls/{ref['pr']}/commits?per_page=100", token))]
        full = [sha for sha in shas if sha.startswith(ref["sha"])]
        if full:
            meta["sha"] = full[0]
        else:
            # No longer one of its commits, after a force push for instance.
            print(f"  {run['id']}: could not resolve commit {ref['sha']} of #{ref['pr']}")
    except (urllib.error.URLError, ValueError, KeyError) as e:
        print(f"  {run['id']}: could not read pull request {ref['pr']} ({e})")
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", required=True)
    parser.add_argument("--workflows", default="Envoy/Checks,Envoy/Prechecks")
    parser.add_argument("--since-hours", type=float, default=2)
    parser.add_argument(
        "--until-hours", type=float, default=0,
        help="End the window this many hours ago rather than now, to collect an older window "
             "in slices.")
    parser.add_argument(
        "--since", default="",
        help="Start of the window, as an ISO 8601 UTC time, in place of --since-hours.")
    parser.add_argument(
        "--until", default="",
        help="End of the window, as an ISO 8601 UTC time, in place of --until-hours.")
    parser.add_argument("--out", default="junit")
    parser.add_argument("--workers", type=int, default=16)
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN")
    workflows = {w.strip() for w in args.workflows.split(",") if w.strip()}
    now = datetime.datetime.now(datetime.timezone.utc)
    since = now - datetime.timedelta(hours=args.since_hours)
    until = now - datetime.timedelta(hours=args.until_hours)
    # Fixed times, for the slices of a window, which do not all start at once.
    if args.since:
        since = datetime.datetime.fromisoformat(args.since.replace("Z", "+00:00"))
    if args.until:
        until = datetime.datetime.fromisoformat(args.until.replace("Z", "+00:00"))

    print(f"GitHub API: {rate_limit(token)}")
    runs = {}
    start = since
    while start < until:
        end = min(start + LIST_SLICE, until)
        created = f"{start:%Y-%m-%dT%H:%M:%SZ}..{end:%Y-%m-%dT%H:%M:%SZ}"
        for run in _github(
                f"/repos/{args.repo}/actions/runs?status=completed&created={created}",
                token, "workflow_runs"):
            if run["name"] in workflows and run["conclusion"] not in ("skipped", "cancelled"):
                runs[run["id"]] = run
        start = end
    runs = list(runs.values())
    print(
        f"{len(runs)} completed runs of {sorted(workflows)} from {since:%Y-%m-%d %H:%M}Z "
        f"to {until:%Y-%m-%d %H:%M}Z")

    run_job_names = {}
    jobs = []
    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        listed = pool.map(
            lambda run: _github(f"/repos/{args.repo}/actions/runs/{run['id']}/jobs", token, "jobs"),
            runs)
        for run, run_jobs in zip(runs, listed):
            run_job_names[run["id"]] = [job["name"] for job in run_jobs]
            jobs += [
                (run, job) for job in run_jobs if job.get("conclusion") not in (None, "skipped")]
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

    with concurrent.futures.ThreadPoolExecutor(args.workers) as pool:
        tests = []
        metas = {}
        for (run, job), found in zip(jobs, pool.map(lambda rj: find_tests(*rj), jobs)):
            job_dir = os.path.join(run_dir(args.out, run), _safe(job["name"]))
            if found:
                totals["jobs"] += 1
                if run["id"] not in metas:
                    metas[run["id"]] = run_meta(args.repo, run, run_job_names[run["id"]], token)
                    metas[run["id"]]["jobs"] = {}
                    os.makedirs(run_dir(args.out, run), exist_ok=True)
                # Keyed by the job's directory, for whatever uploads this to tell the jobs apart.
                metas[run["id"]]["jobs"][os.path.basename(job_dir)] = {
                    "name": job["name"],
                    "leg": job_leg(job["name"]),
                    "id": job["id"],
                    "url": job.get("html_url", ""),
                }
            for invocation, labels in found:
                totals["invocations"] += 1
                tests += [(job_dir, invocation, label) for label in labels]
        for run in runs:
            if run["id"] in metas:
                with open(os.path.join(run_dir(args.out, run), "run.json"), "w") as f:
                    json.dump(metas[run["id"]], f)

    # Each test's outputs, then the XML among them, some of the tests at a time.
    for start in range(0, len(tests), FETCH_TESTS):
        fetch = tests[start:start + FETCH_TESTS]
        targets = get_targets([(invocation, label) for _, invocation, label in fetch])
        outputs = [
            [digest for digest in _digests(target) if digest[1] <= MAX_XML_BYTES]
            for target in targets]
        blobs = read_blobs([digest for digests in outputs for digest in digests])
        for (job_dir, invocation, label), digests in zip(fetch, outputs):
            written = 0
            for digest in digests:
                blob = blobs.get(digest, b"")
                if not blob.lstrip().startswith(b"<?xml"):
                    continue
                # Named after the test, as that is what the file's results are listed under once
                # uploaded. A sharded or repeated test has several, hence the directory.
                path = os.path.join(
                    job_dir, invocation, digest[0][:12], f"{_safe(label.lstrip('/'))}.xml")
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(blob)
                written += 1
            totals["tests"] += 1
            totals["xml"] += written
            totals["missing"] += not written

    summary = ", ".join(f"{k}: {v}" for k, v in totals.items())
    print(summary)
    print(f"GitHub API: {rate_limit(token)}")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write(f"### JUnit collected from EngFlow\n\n{len(runs)} runs; {summary}\n")


if __name__ == "__main__":
    sys.exit(main())
