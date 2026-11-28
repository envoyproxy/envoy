#!/usr/bin/env python3
"""Fetch what a report of postsubmit CI is made from: commits, their checks, and failed jobs.

All Envoy CI runs as `workflow_run` on `main`, whatever change it is testing, so the Actions
run list cannot tell a postsubmit run from a PR run. The CI app does however post one check per
requested workflow on the commit it is testing, with the workflow run id as the check's
`external_id`. `commits` walks the commits pushed to each branch in a week or a month and writes
those checks, as GitHub gives them. `jobs` writes the jobs of the workflow runs it is given.

Nothing is worked out here beyond the period: the toolshed `ci/checks` jq module says which runs
to fetch the jobs of, and turns what is fetched into the report and renders it.

Weeks run from Sunday to Sunday and months from the 1st, at midnight UTC, so that reports can be
compiled. A period which has not ended yet is fetched to date.

A commit is in the period it was committed in, which for a squashed merge is when it was merged.
A stack which is pushed without being rewritten is tested when it is pushed but counted when it
was committed, which can be the period before.
"""

import argparse
import concurrent.futures
import datetime
import fnmatch
import http.client
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

GITHUB_API = "https://api.github.com"
# Of what is written, which the jq module also has
SCHEMA = 6
# The `ci-envoy` app, which posts the checks. Workflow tokens cannot look an app up by its slug.
CI_APP_ID = 419860

# Commits with their checks, a page at a time: far fewer requests than a call per commit, which
# matters as only the head commit of a push has any checks.
COMMITS_QUERY = """
query($owner: String!, $name: String!, $ref: String!, $since: GitTimestamp!,
      $until: GitTimestamp!, $app: Int!, $cursor: String) {
  repository(owner: $owner, name: $name) {
    ref(qualifiedName: $ref) {
      target {
        ... on Commit {
          history(first: 50, since: $since, until: $until, after: $cursor) {
            pageInfo { hasNextPage endCursor }
            nodes {
              oid
              committedDate
              messageHeadline
              checkSuites(first: 20, filterBy: {appId: $app}) {
                pageInfo { hasNextPage }
                nodes {
                  checkRuns(first: 100, filterBy: {checkType: ALL}) {
                    pageInfo { hasNextPage }
                    nodes { name status conclusion externalId text }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
"""


def _request(url, token, data=None):
    for attempt in range(6):
        req = urllib.request.Request(url, data=data)
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            limited = e.code == 429 or (
                e.code == 403 and e.headers.get("x-ratelimit-remaining") == "0")
            if limited:
                reset = int(e.headers.get("x-ratelimit-reset") or 0)
                wait = max(reset - time.time(), 0) + 5
                print(f"Rate limited, waiting {wait:.0f}s", file=sys.stderr)
                time.sleep(wait)
            elif e.code >= 500:
                time.sleep(5 * (attempt + 1))
            else:
                raise
        except (OSError, http.client.HTTPException):
            # Timeouts and dropped connections
            time.sleep(5 * (attempt + 1))
    raise RuntimeError(f"Giving up on {url}")


def _get(path, token, **params):
    url = f"{GITHUB_API}{path}"
    if params:
        url = f"{url}?{urllib.parse.urlencode(params)}"
    return _request(url, token)


def _paged(path, token, key=None, **params):
    items, page = [], 1
    while True:
        data = _get(path, token, per_page=100, page=page, **params)
        chunk = data[key] if key else data
        items += chunk
        if len(chunk) < 100:
            return items
        page += 1


def _graphql(query, token, **variables):
    body = json.dumps(dict(query=query, variables=variables)).encode()
    attempt = 0
    while attempt < 4:
        data = _request(f"{GITHUB_API}/graphql", token, body)
        if not data.get("errors"):
            return data["data"]
        print(f"GraphQL: {data['errors'][0].get('message')}", file=sys.stderr)
        if any(error.get("type") == "RATE_LIMITED" for error in data["errors"]):
            # Unlike the REST API this is not an HTTP error, and does not say when it ends.
            reset = _get("/rate_limit", token)["resources"]["graphql"]["reset"]
            wait = max(reset - time.time(), 0) + 5
            print(f"Rate limited, waiting {wait:.0f}s", file=sys.stderr)
            time.sleep(wait)
            continue
        # Mostly timeouts, which pass.
        attempt += 1
        time.sleep(10 * attempt)
    raise RuntimeError(f"GraphQL query failed: {data['errors']}")


def _commits(repo, token, app_id, branch, since, until):
    """The branch's commits in the period, each with its checks."""
    owner, name = repo.split("/")
    commits, cursor = [], None
    while True:
        data = _graphql(
            COMMITS_QUERY,
            token,
            owner=owner,
            name=name,
            ref=f"refs/heads/{branch}",
            since=since,
            until=until,
            app=app_id,
            cursor=cursor)
        ref = data["repository"]["ref"]
        if not ref:
            return []
        history = ref["target"]["history"]
        for node in history["nodes"]:
            suites = node["checkSuites"]
            if suites["pageInfo"]["hasNextPage"] or any(
                    suite["checkRuns"]["pageInfo"]["hasNextPage"] for suite in suites["nodes"]):
                # A request makes a check per workflow, so this is a commit which was requested
                # many times over. Better to stop than to report on some of its checks.
                raise SystemExit(f"Commit {node['oid']} has more checks than are fetched")
        commits += [
            dict(
                sha=node["oid"],
                date=node["committedDate"],
                title=node["messageHeadline"],
                checks=[
                    dict(
                        name=check["name"],
                        status=check["status"],
                        conclusion=check["conclusion"],
                        run_id=check["externalId"],
                        text=check["text"]) for suite in node["checkSuites"]["nodes"]
                    for check in suite["checkRuns"]["nodes"]
                ]) for node in history["nodes"]
        ]
        if not history["pageInfo"]["hasNextPage"]:
            return commits
        cursor = history["pageInfo"]["endCursor"]


def _jobs(repo, token, run_id):
    jobs = _paged(f"/repos/{repo}/actions/runs/{run_id}/jobs", token, "jobs", filter="latest")
    return [dict(name=job["name"], conclusion=job["conclusion"]) for job in jobs]


def _version_key(branch):
    return [int(part) for part in re.findall(r"\d+", branch)]


def _branches(repo, token, patterns):
    """Resolve branch names and patterns (`release/v*`), patterns newest version first."""
    branches = []
    for pattern in filter(None, (p.strip() for p in patterns.split(","))):
        prefix = re.split(r"[*?\[]", pattern)[0]
        if prefix == pattern:
            found = [pattern]
        else:
            # All of them at once: this endpoint does not page.
            refs = _get(f"/repos/{repo}/git/matching-refs/heads/{prefix}", token)
            names = (ref["ref"].removeprefix("refs/heads/") for ref in refs)
            found = sorted(
                (name for name in names if fnmatch.fnmatchcase(name, pattern)),
                key=_version_key,
                reverse=True)
        branches += [branch for branch in found if branch not in branches]
    return branches


def _week_of(day):
    """The Sunday which starts the week `day` is in."""
    # Monday is 0
    return day - datetime.timedelta(days=(day.weekday() + 1) % 7)


def _frame(period, day=None):
    """Start, end and name of the period containing `day`, or of the last complete one."""
    today = datetime.datetime.now(datetime.timezone.utc).date()
    if period == "month":
        start = datetime.date.fromisoformat(day) if day else today.replace(day=1)
        start = start.replace(day=1)
        if not day:
            start = (start - datetime.timedelta(days=1)).replace(day=1)
        end = (start + datetime.timedelta(days=32)).replace(day=1)
        name = f"{start:%Y-%m}"
    else:
        start = _week_of(
            datetime.date.fromisoformat(day) if day else today - datetime.timedelta(days=7))
        end = start + datetime.timedelta(days=7)
        name = start.isoformat()
    midnight = datetime.time(tzinfo=datetime.timezone.utc)
    return (
        datetime.datetime.combine(start, midnight), datetime.datetime.combine(end, midnight), name)


def _stamp(value):
    return value.strftime("%Y-%m-%dT%H:%M:%SZ")


def commits(args, token):
    since, until, name = _frame(args.period, args.date)
    now = datetime.datetime.now(datetime.timezone.utc)
    raw = dict(
        schema=SCHEMA,
        repo=args.repo,
        period=args.period,
        frame=name,
        complete=now >= until,
        generated=_stamp(now),
        since=_stamp(since),
        until=_stamp(until),
        branches={})
    for branch in _branches(args.repo, token, args.branches):
        # `until` is inclusive in the API, and the next period starts on it.
        found = _commits(
            args.repo, token, args.app_id, branch, _stamp(since),
            _stamp(until - datetime.timedelta(seconds=1)))
        if found:
            raw["branches"][branch] = found
            print(f"{branch}: {len(found)} commits", file=sys.stderr)
    print(f"{args.period}-{name}")
    return raw


def jobs(args, token):
    with open(args.runs) as f:
        runs = [str(run) for run in json.load(f)]
    with concurrent.futures.ThreadPoolExecutor(4) as executor:
        found = executor.map(lambda run: _jobs(args.repo, token, run), runs)
        print(f"{len(runs)} runs", file=sys.stderr)
        return dict(zip(runs, found))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", default="envoyproxy/envoy")
    parser.add_argument("--out", required=True, help="Where to write what is fetched (JSON).")
    commands = parser.add_subparsers(dest="command", required=True)
    parser_commits = commands.add_parser("commits", help="Commits and their checks.")
    parser_commits.add_argument(
        "--branches",
        default="main,release/v*",
        help="Comma-separated branches or patterns. Those with no commits are omitted.")
    parser_commits.add_argument("--period", choices=("week", "month"), default="week")
    parser_commits.add_argument(
        "--date",
        help="Any date (YYYY-MM-DD) in the period to fetch. Defaults to the last complete one.")
    parser_commits.add_argument(
        "--app-id", type=int, default=CI_APP_ID, help="Id of the app which posts the checks.")
    parser_jobs = commands.add_parser("jobs", help="The jobs of workflow runs.")
    parser_jobs.add_argument("--runs", required=True, help="File with a JSON list of run ids.")
    args = parser.parse_args()
    token = os.environ.get("GITHUB_TOKEN")

    fetched = dict(commits=commits, jobs=jobs)[args.command](args, token)
    with open(args.out, "w") as f:
        json.dump(fetched, f)
        f.write("\n")


if __name__ == "__main__":
    main()
