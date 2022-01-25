# Script for collecting PRs in need of review, and informing reviewers via
# slack.
#
# By default this runs in "developer mode" which means that it collects PRs
# associated with reviewers and API reviewers, and spits them out (badly
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

REVIEWERS = {
    'alyssawilk': 'U78RP48V9',
    'Augustyniak': 'U017R1YHXGQ',
    'buildbreaker': 'UEUEP1QP4',
    'jpsim': 'U02KAPRELKA',
    'junr03': 'U79K0Q431',
    'RyanTheOptimist': 'U01SW3JC8GP',
    'goaway': 'U7TDPD3L2',
    'snowp': 'U93KTPQP6',
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
        assignees, assignees_and_prs, message, primary_assignee_map):
    has_primary_assignee = False
    for assignee_info in assignees:
        assignee = assignee_info.login
        if assignee in primary_assignee_map:
            has_primary_assignee = True
        if assignee not in assignees_and_prs.keys():
            assignees_and_prs[
                assignee] = "Hello, %s, here are your PR reminders for the day \n" % assignee
        assignees_and_prs[assignee] = assignees_and_prs[assignee] + message
    return has_primary_assignee


def track_prs():
    git = github.Github()
    repo = git.get_repo('envoyproxy/envoy-mobile')

    # A dict of maintainer : outstanding_pr_string to be sent to slack
    reviewers_and_prs = {}
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
        # envoy-mobile currently doesn't triage unassigned PRs.
        if not(pr_info.assignees):
            continue

        # Update the time based on the time zone delta from github's
        pr_age = pr_info.updated_at - datetime.timedelta(hours=4)
        delta = datetime.datetime.now() - pr_age
        delta_days = delta.days
        delta_hours = delta.seconds // 3600

        # If we get to this point, the review may be in SLO - nudge if it's in
        # SLO, nudge in bold if not.
        message = pr_message(delta, pr_info.html_url, pr_info.title, delta_days, delta_hours)

        # If the PR has been out-SLO for over a day, inform maintainers.
        if delta > datetime.timedelta(hours=get_slo_hours() + 36):
            stalled_prs = stalled_prs + message

        # Add a reminder to each maintainer-assigner on the PR.
        add_reminders(pr_info.assignees, reviewers_and_prs, message, REVIEWERS)

    # Return the dict of {reviewers : PR notifications},
    # and stalled PRs
    return reviewers_and_prs, stalled_prs


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
            response = client.chat_postMessage(channel=channel_id, text=message)
        except SlackApiError as e:
            print("Unexpected error %s", e.response["error"])


def post_to_oncall(client, out_slo_prs):
    try:
        response = client.chat_postMessage(
            channel='#envoy-mobile-oncall', text=("*Stalled PRs*\n\n%s" % out_slo_prs))
    except SlackApiError as e:
        print("Unexpected error %s", e.response["error"])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--cron_job',
        action="store_true",
        help="true if this is run by the daily cron job, false if run manually by a developer")
    args = parser.parse_args()

    reviewers_and_messages, stalled_prs = track_prs()

    if not args.cron_job:
        print(reviewers_and_messages)
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
    post_to_oncall(client, reviewers_and_messages['unassigned'], stalled_prs)
    post_to_assignee(client, reviewers_and_messages, REVIEWERS)
