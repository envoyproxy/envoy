#!/usr/bin/env bash

set -euo pipefail

#######################################################
# bump_lyft_support_rotation.sh
#
# Assigns the next person in the Lyft maintainers list.
#######################################################

# TODO(jpsim): Use a yaml parsing library if the format ever expands
# beyond its current very simple form.

function next_maintainer() {
  current="$1"
  file="$2"

  while IFS= read -r line; do
    maintainers+=("${line#"  - "}")
  done <<< "$(sed -n '/^  - /p' "$file")"

  for i in "${!maintainers[@]}"; do
    if [[ "${maintainers[$i]}" = "${current}" ]]; then
      last_index=$((${#maintainers[@]}-1))
      if [[ $i == "$last_index" ]]; then
        echo "${maintainers[0]}"
        break
      else
        echo "${maintainers[$((i + 1))]}"
        break
      fi
    fi
  done
}

function set_maintainer() {
  maintainer="$1"
  file="$2"

  echo "current: $maintainer" > "$file.tmp"
  tail -n +2 "$file" >> "$file.tmp"
  mv "$file.tmp" "$file"
}

maintainers_file=".github/lyft_maintainers.yml"
first_line="$(head -n 1 "$maintainers_file")"
current=${first_line#"current: "}
next="$(next_maintainer "$current" "$maintainers_file")"
set_maintainer "$next" "$maintainers_file"

message="Lyft support maintainer changing from <https://github.com/$current|$current> to <https://github.com/$next|$next>"
echo "$message"

curl -H "Content-type: application/json" \
  --data "{\"channel\":\"C02F93EEJCE\",\"blocks\":[{\"type\":\"section\",\"text\":{\"type\":\"mrkdwn\",\"text\":\"$message\"}}]}" \
  -H "Authorization: Bearer $SLACK_BOT_TOKEN" \
  -X POST \
  https://slack.com/api/chat.postMessage
