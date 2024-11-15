#!/usr/bin/env bash

set -euo pipefail

##################################################################
# post_to_slack.sh
#
# Posts a message to the #envoy-mobile-collaboration Slack channel
#
# Usage: `SLACK_BOT_TOKEN=x post_to_slack.sh "markdown message"`
##################################################################

curl -H "Content-type: application/json; charset=utf-8" \
  --data "{\"channel\":\"C02F93EEJCE\",\"blocks\":[{\"type\":\"section\",\"text\":{\"type\":\"mrkdwn\",\"text\":\"$1\"}}]}" \
  -H "Authorization: Bearer $SLACK_BOT_TOKEN" \
  -X POST \
  https://slack.com/api/chat.postMessage
