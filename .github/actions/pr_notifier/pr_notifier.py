# Script for collecting PRs in need of review, and informing maintainers via
# slack.
#
# By default this runs in "developer mode" which means that it collects PRs
# associated with maintainers and API reviewers, and spits them out (badly
# formatted) to the command line.
#
# .github/workflows/pr_notifier.yml runs the script with --cron_job
# which instead sends the collected PRs to the various slack channels.
#
# NOTE: Slack IDs can be found in the user's full profile from within Slack.

from __future__ import print_function

import argparse
import datetime
import os
import sys

import github
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError

MAINTAINERS = {
    'alyssawilk': 'U78RP48V9',
    'dio': 'U79S2DFV1',
    'mattklein123': 'U5CALEVSL',
    'lizan': 'U79E51EQ6',
    'snowp': 'U93KTPQP6',
    'ggreenway': 'U78MBV869',
    'htuch': 'U78E7055Z',
    'zuercher': 'U78J72Q82',
    'phlax': 'U017PLM0GNQ',
    'jmarantz': 'U80HPLBPG',
    'antoniovicente': 'UKVNCQ212',
    'wrowe': 'UBQR8NGBS',
    'yanavlasov': 'UJHLR5KFS',
    'rojkov': 'UH5EXLYQK',
    'RyanTheOptimist': 'U01SW3JC8GP',
    'adisuissa': 'UT17EMMTP',
    'KBaichoo': 'U016ZPU8KBK',
    'wbpcode': 'U017KF5C0Q6',
}

# First pass reviewers who are not maintainers should get
# notifications but not result in a PR not getting assigned a
# maintainer owner.
FIRST_PASS = {
    'dmitri-d': 'UB1883Q5S',
    'tonya11en': 'U989BG2CW',
    'esmet': 'U01BCGBUUAE',
    'mathetake': 'UG9TD2FSB',
}

# Only notify API reviewers who aren't maintainers.
# Maintainers are already notified of pending PRs.
API_REVIEWERS = {
    'markdroth': 'UMN8K55A6',
    'adisuissa': 'UT17EMMTP',
}


def get_slo_hours():
    # on Monday, allow for 24h + 48h
    if datetime.date.today().weekday() == 0:
        return 72
    return 24


# Return true if the PR has a waiting tag, false otherwise.
def is_waiting(labels):
    for label in labels:
        if label.name == 'waiting' or label.name == 'waiting:any':
            return True
    return False


def is_contrib(labels):
    return any(label.name == "contrib" for label in labels)


# Return true if the PR has an API tag, false otherwise.
def is_api(labels):
    for label in labels:
        if label.name == 'api':
            return True
    return False


# Generate a pr message, bolding the time if it's out-SLO
def pr_message(pr_age, pr_url, pr_title, delta_days, delta_hours):
    if pr_age < datetime.timedelta(hours=get_slo_hours()):
        return "<%s|%s> has been waiting %s days %s hours\n" % (
            pr_url, pr_title, delta_days, delta_hours)
    else:
        return "<%s|%s> has been waiting *%s days %s hours*\n" % (
            pr_url, pr_title, delta_days, delta_hours)


# Adds reminder lines to the appropriate assignee to review the assigned PRs
# Returns true if one of the assignees is in the primary_assignee_map, false otherwise.
def add_reminders(
        assignees, assignees_and_prs, message, primary_assignee_map, first_pass_assignee_map):
    has_primary_assignee = False
    for assignee_info in assignees:
        assignee = assignee_info.login
        if assignee in primary_assignee_map:
            has_primary_assignee = True
        elif assignee not in first_pass_assignee_map:
            continue
        if assignee not in assignees_and_prs.keys():
            assignees_and_prs[
                assignee] = "Hello, %s, here are your PR reminders for the day \n" % assignee
        assignees_and_prs[assignee] = assignees_and_prs[assignee] + message
    return has_primary_assignee


# Returns true if the PR needs an LGTM from an API shephard.
def needs_api_review(labels, repo, pr_info):
    # API reviews should always have the label, so don't bother doing an RPC if
    # it's not tagged (this helps avoid github rate limiting)
    if not (is_api(labels)):
        return False
    # repokitten tags each commit as pending unless there has been an API LGTM
    # since the latest API changes. If this PR is tagged pendding it needs an
    # API review, otherwise it's set.
    headers, data = repo._requester.requestJsonAndCheck(
        "GET",
        ("https://api.github.com/repos/envoyproxy/envoy/statuses/" + pr_info.head.sha),
    )
    if (data and data[0]["state"] == 'pending'):
        return True
    return False


def track_prs():
    git = github.Github()
    repo = git.get_repo('envoyproxy/envoy')

    # The list of PRs which are not waiting, but are well within review SLO
    recent_prs = []
    # A dict of maintainer : outstanding_pr_string to be sent to slack
    maintainers_and_prs = {}
    # A placeholder for unassigned PRs, to be sent to #maintainers eventually
    maintainers_and_prs['unassigned'] = ""
    # A dict of shephard : outstanding_pr_string to be sent to slack
    api_review_and_prs = {}
    # Out-SLO PRs to be sent to #envoy-maintainer-oncall
    stalled_prs = ""

    # Snag all PRs, including drafts
    for pr_info in repo.get_pulls("open", "updated", "desc"):
        labels = pr_info.labels
        assignees = pr_info.assignees
        # If the PR is waiting, continue.
        if is_waiting(labels):
            continue
        # Drafts are not covered by our SLO (repokitteh warns of this)
        if pr_info.draft:
            continue
        # Don't warn for dependabot.
        if pr_info.user.login == 'dependabot[bot]':
            continue

        # Update the time based on the time zone delta from github's
        pr_age = pr_info.updated_at - datetime.timedelta(hours=4)
        delta = datetime.datetime.now() - pr_age
        delta_days = delta.days
        delta_hours = delta.seconds // 3600

        # If we get to this point, the review may be in SLO - nudge if it's in
        # SLO, nudge in bold if not.
        message = pr_message(delta, pr_info.html_url, pr_info.title, delta_days, delta_hours)

        if (needs_api_review(labels, repo, pr_info)):
            add_reminders(pr_info.assignees, api_review_and_prs, message, API_REVIEWERS, [])

        # If the PR has been out-SLO for over a day, inform on-call
        if delta > datetime.timedelta(hours=get_slo_hours() + 36):
            stalled_prs = stalled_prs + message

        # Add a reminder to each maintainer-assigner on the PR.
        has_maintainer_assignee = add_reminders(
            pr_info.assignees, maintainers_and_prs, message, MAINTAINERS, FIRST_PASS)

        # If there was no maintainer, track it as unassigned.
        if not has_maintainer_assignee and not is_contrib(labels):
            maintainers_and_prs['unassigned'] = maintainers_and_prs['unassigned'] + message

    # Return the dict of {maintainers : PR notifications},
    # the dict of {api-shephards-who-are-not-maintainers: PR notifications},
    # and stalled PRs
    return maintainers_and_prs, api_review_and_prs, stalled_prs


def post_to_assignee(client, assignees_and_messages, assignees_map):
    # Post updates to individual assignees
    for key in assignees_and_messages:
        message = assignees_and_messages[key]

        # Only send messages if we have the slack UID
        if key not in assignees_map:
            continue
        uid = assignees_map[key]

        # Ship messages off to slack.
        try:
            print(assignees_and_messages[key])
            response = client.conversations_open(users=uid, text="hello")
            channel_id = response["channel"]["id"]
            client.chat_postMessage(channel=channel_id, text=message)
        except SlackApiError as e:
            print("Unexpected error %s", e.response["error"])


def post_to_oncall(client, unassigned_prs, out_slo_prs):
    # Post updates to #envoy-maintainer-oncall
    unassigned_prs = maintainers_and_messages['unassigned']
    try:
        client.chat_postMessage(
            channel='#envoy-maintainer-oncall',
            text=("*'Unassigned' PRs* (PRs with no maintainer assigned)\n%s" % unassigned_prs))
        client.chat_postMessage(
            channel='#envoy-maintainer-oncall',
            text=("*Stalled PRs* (PRs with review out-SLO, please address)\n%s" % out_slo_prs))
        issue_link = "https://github.com/envoyproxy/envoy/issues?q=is%3Aissue+is%3Aopen+label%3Atriage"
        client.chat_postMessage(
            channel='#envoy-maintainer-oncall',
            text=(
                "*Untriaged Issues* (please tag and cc area experts)\n<%s|%s>" %
                (issue_link, issue_link)))
    except SlackApiError as e:
        print("Unexpected error %s", e.response["error"])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--cron_job',
        action="store_true",
        help="true if this is run by the daily cron job, false if run manually by a developer")
    args = parser.parse_args()

    maintainers_and_messages, shephards_and_messages, stalled_prs = track_prs()

    if not args.cron_job:
        print(maintainers_and_messages)
        print("\n\n\n")
        print(shephards_and_messages)
        print("\n\n\n")
        print(stalled_prs)
        exit(0)

    SLACK_BOT_TOKEN = os.getenv('SLACK_BOT_TOKEN')
    if not SLACK_BOT_TOKEN:
        print(
            'Missing SLACK_BOT_TOKEN: please export token from https://api.slack.com/apps/A023NPQQ33K/oauth?'
        )
        sys.exit(1)

    client = WebClient(token=SLACK_BOT_TOKEN)
    post_to_oncall(client, maintainers_and_messages['unassigned'], stalled_prs)
    post_to_assignee(client, shephards_and_messages, API_REVIEWERS)
    post_to_assignee(client, maintainers_and_messages, MAINTAINERS)
    post_to_assignee(client, maintainers_and_messages, FIRST_PASS)
