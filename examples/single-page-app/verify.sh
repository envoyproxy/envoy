#!/bin/bash -e

# Note: This uses the pip version of yq, which is a simple
#  wrapper taking jq args - not the separate package installed
#  with eg snap

export NAME=single-page-app
export PORT_DEV_PROXY="${SPA_PORT_DEV_PROXY:-11901}"
export PORT_PROXY="${SPA_PORT_PROXY:-11900}"
export PORT_MYHUB="${SPA_PORT_MYHUB:-11902}"
export MANUAL=true


BACKUP_FILES=(
  "envoy.yml"
)


finally () {
    rm -rf .local.ci
    for file in "${BACKUP_FILES[@]}"; do
        move_if_exists "${file}.bak" "${file}"
    done
}

export -f finally

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# As much of the logic is implemented in js, not everything can get tested, at least without using
# eg selenium or similar.
# Everything else should be tested.


EXPECTED_USER_JQ=$(
cat << 'EOF'
{"avatar_url": "http://localhost:\($port)/images/users/envoy.svg",
 "followers": 3,
 "following": 2,
 "name": "Envoy Demo",
 "login": "envoydemo",
 "public_repos": 3}
EOF
)
EXPECTED_USER="$(
    yq -c \
        --arg port "$PORT_MYHUB" \
        "$EXPECTED_USER_JQ" \
      < myhub/data.yml)"

EXPECTED_REPOS_JQ=$(
cat << 'EOF'
.users.envoydemo.public_repos as $user_repos
| .repos as $repos
| $user_repos
| map({
    "html_url": "http://localhost:\($port)/envoydemo/\(.)",
    "updated_at": $repos[.].updated_at,
    "full_name": "envoydemo/\(.)"})
EOF
)
EXPECTED_REPOS="$(
    yq -c \
        --arg port "$PORT_MYHUB" \
        "$EXPECTED_REPOS_JQ" \
      < myhub/data.yml)"

EXPECTED_FOLLOWERS_JQ=$(
cat << 'EOF'
.users.envoydemo.followers as $followers
| .users as $users
| $followers
| map({
    "avatar_url": "http://localhost:\($port)/images/users/\(.).png",
    "name": $users[.].name,
    "html_url": "http://localhost:\($port)/users/\(.)",
    "login": .})
EOF
)
EXPECTED_FOLLOWING="$(
    yq -c \
        --arg port "$PORT_MYHUB" \
        "$EXPECTED_FOLLOWERS_JQ" \
      < myhub/data.yml)"

EXPECTED_FOLLOWING_JQ=$(
cat << 'EOF'
.users.envoydemo.following as $following
| .users as $users
| $following
| map({
    "avatar_url": "http://localhost:\($port)/images/users/\(.).png",
    "name": $users[.].name,
    "html_url": "http://localhost:\($port)/users/\(.)",
    "login": .})
EOF
)
EXPECTED_FOLLOWING="$(
    yq -c \
        --arg port "$PORT_MYHUB" \
        "$EXPECTED_FOLLOWING_JQ" \
      < myhub/data.yml)"


test_auth () {
    local proxy_port
    proxy_scheme=$1
    proxy_port=$2
    curl_args=()
    if [[ "$proxy_scheme" == "https" ]]; then
        curl_args=(-k)
    fi

    run_log "Fetch the app page"
    responds_with \
        "Envoy single page app example" \
        "${proxy_scheme}://localhost:${proxy_port}" \
        "${curl_args[@]}"

    run_log "Inititiate login"
    responds_with_header \
        "HTTP/1.1 302 Found" \
        "${proxy_scheme}://localhost:${proxy_port}/login" \
        "${curl_args[@]}"
    responds_with_header \
        "location: http://localhost:${PORT_MYHUB}/authorize?client_id=0123456789&redirect_uri=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Fauthorize&response_type=code&scope=user%3Aemail&state=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Flogin" \
        "${proxy_scheme}://localhost:${proxy_port}/login" \
        "${curl_args[@]}"

    run_log "Fetch the myhub authorization page"
    responds_with_header \
        "HTTP/1.1 302 Found" \
        "http://localhost:${PORT_MYHUB}/authorize?client_id=0123456789&redirect_uri=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Fauthorize&response_type=code&scope=user%3Aemail&state=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Flogin" \
        "${curl_args[@]}"
    responds_with_header \
        "Location: ${proxy_scheme}://localhost:${proxy_port}/authorize?code=" \
        "http://localhost:${PORT_MYHUB}/authorize?client_id=0123456789&redirect_uri=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Fauthorize&response_type=code&scope=user%3Aemail&state=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Flogin" \
        "${curl_args[@]}"

    run_log "Return to the app and receive creds"
    CODE=$(_curl "${curl_args[@]}" --head "http://localhost:${PORT_MYHUB}/authorize?client_id=0123456789&redirect_uri=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Fauthorize&response_type=code&scope=user%3Aemail&state=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Flogin" | grep Location | cut -d= -f2 | cut -d\& -f1)
    RESPONSE=$(_curl "${curl_args[@]}" --head "${proxy_scheme}://localhost:${proxy_port}/authorize?code=$CODE&state=${proxy_scheme}%3A%2F%2Flocalhost%3A${proxy_port}%2Flogin")
    echo "$RESPONSE" | grep "HTTP/1.1 302 Found"
    echo "$RESPONSE" | grep "location: ${proxy_scheme}://localhost:${proxy_port}/login"
    echo "$RESPONSE" | grep "set-cookie: OauthHMAC="
    echo "$RESPONSE" | grep "set-cookie: OauthExpires="
    echo "$RESPONSE" | grep "set-cookie: BearerToken="

    HMAC=$(echo "$RESPONSE" | grep "set-cookie: OauthHMAC=" | cut -d' ' -f2-)
    OAUTH_EXPIRES=$(echo "$RESPONSE" | grep "set-cookie: OauthExpires=" | cut -d' ' -f2-)
    TOKEN=$(echo "$RESPONSE" | grep "set-cookie: BearerToken=" | cut -d' ' -f2-)
    COOKIES=(
        --cookie "$HMAC"
        --cookie "$OAUTH_EXPIRES"
        --cookie "$TOKEN")

    endpoints=(
        "Fetch user object|${EXPECTED_USER}|/hub/user"
        "Fetch repos|${EXPECTED_REPOS}|/hub/users/envoydemo/repos"
        "Fetch followers|${EXPECTED_FOLLOWERS}|/hub/users/envoydemo/followers"
        "Fetch following|${EXPECTED_FOLLOWING}|/hub/users/envoydemo/following"
    )

    for endpoint in "${endpoints[@]}"; do
        IFS='|' read -r log_message expected_response path <<< "$endpoint"
        run_log "$log_message"
        responds_with \
            "$expected_response" \
            "${proxy_scheme}://localhost:${proxy_port}${path}" \
            "${COOKIES[@]}" \
            "${curl_args[@]}"
    done

    run_log "Log out of Myhub"
    RESPONSE=$(_curl "${curl_args[@]}" --head "${proxy_scheme}://localhost:${proxy_port}/logout")
    echo "$RESPONSE" | grep "HTTP/1.1 302 Found"
    echo "$RESPONSE" | grep "location: ${proxy_scheme}://localhost:${proxy_port}/"
    echo "$RESPONSE" | grep "set-cookie: OauthHMAC=deleted"
    echo "$RESPONSE" | grep "set-cookie: BearerToken=deleted"
}

get_js () {
    _curl -k "https://localhost:${PORT_PROXY}" \
        | grep "assets/index" \
        | grep -oP '<script type="module" crossorigin src="/assets/[^"]+"></script>' \
        | grep -oP '/assets/[^"]+' \
        | sed 's/\/assets\///;s/".*//'
}

run_log "Adjust environment for CI"
# This is specific to verify.sh script and so slightly adjust from docs.
rm -rf .local.ci
mkdir -p .local.ci
cp -a ui .local.ci/
export UI_PATH=./.local.ci/ui
for file in "${BACKUP_FILES[@]}"; do
    cp -a "${file}" "${file}.bak"
done
echo "VITE_APP_API_URL=https://localhost:${PORT_PROXY}" > ui/.env.production.local
echo "VITE_APP_API_URL=http://localhost:${PORT_DEV_PROXY}" > ui/.env.development.local
sed -i "s/localhost:7000/localhost:${PORT_MYHUB}/g" envoy.yml
export UID

run_log "Generate an HMAC secret"
cp -a secrets/ .local.ci/
export SECRETS_PATH=./.local.ci/secrets/
HMAC_SECRET=$(echo "MY_HMAC_SECRET" | mkpasswd -s)
export HMAC_SECRET
envsubst < hmac-secret.tmpl.yml > .local.ci/secrets/hmac-secret.yml

run_log "Start servers"
bring_up_example

test_auth http "${PORT_DEV_PROXY}"

run_log "Live reload dev app"
sed -i s/Envoy\ single\ page\ app\ example/CUSTOM\ APP/g .local.ci/ui/index.html
responds_with \
    "CUSTOM APP" \
    "http://localhost:${PORT_DEV_PROXY}"

run_log "Run yarn lint"
docker compose run --rm ui yarn lint

run_log "Build the production app"
mkdir -p .local.ci/production
cp -a xds .local.ci/production
cp -a ui/index.html .local.ci/ui/
export XDS_PATH=./.local.ci/production/xds
docker compose up --build -d envoy
docker compose run --rm ui build.sh

run_log "Check the created routes"
jq '.resources[0].filter_chains[0].filters[0].typed_config.route_config.virtual_hosts[0].routes' < .local.ci/production/xds/lds.yml

test_auth https "${PORT_PROXY}"

current_js=$(get_js)

run_log "Ensure assets are served with compression"
responds_with_header \
    "content-encoding: gzip" \
    "https://localhost:${PORT_PROXY}/assets/${current_js}" \
    -k -i -H "Accept-Encoding: gzip"

run_log "Rebuild production app"
sed -i s/Login\ to\ query\ APIs/LOGIN\ NOW/g .local.ci/ui/src/components/Home.tsx
docker compose run --rm ui build.sh
wait_for 5 \
    bash -c "\
        responds_without \
            \"$current_js\" \
            \"https://localhost:${PORT_PROXY}\" \
            \"-k\""
responds_with \
    "Envoy single page app example" \
    "https://localhost:${PORT_PROXY}" \
    -k

run_log "Update Envoy's configuration to use Github"
export TOKEN_SECRET=ZZZ
envsubst < token-secret.tmpl.yml > .local.ci/secrets/github-token-secret.yml
GITHUB_PROVIDED_CLIENT_ID=XXX
cp -a envoy.yml .local.ci/
sed -i "s@cluster:\ hub@cluster:\ github@g" .local.ci/envoy.yml
sed -i "s@client_id:\ \"0123456789\"@client_id:\ \"$GITHUB_PROVIDED_CLIENT_ID\"@g" .local.ci/envoy.yml
sed -i "s@authorization_endpoint:\ http://localhost:${PORT_MYHUB}/authorize@authorization_endpoint:\ https://github.com/login/oauth/authorize@g" .local.ci/envoy.yml
sed -i "s@uri:\ http://myhub:${PORT_MYHUB}/authenticate@uri:\ https://github.com/login/oauth/access_token@g" .local.ci/envoy.yml
sed -i "s@path:\ /etc/envoy/secrets/myhub-token-secret.yml@path:\ /etc/envoy/secrets/github-token-secret.yml@g" .local.ci/envoy.yml
sed -i "s@host_rewrite_literal:\ api.myhub@host_rewrite_literal:\ api.github.com@g" .local.ci/envoy.yml
cat _github-clusters.yml >> .local.ci/envoy.yml

run_log "Update the app configuration to use Github"
echo "VITE_APP_AUTH_PROVIDER=github" > .local.ci/ui/.env.local

run_log "Rebuild the app and restart Envoy (Github)"
export ENVOY_CONFIG=.local.ci/envoy.yml
docker compose run --rm ui build.sh
docker compose up --build -d envoy

run_log "Test dev app (Github)"
wait_for 5 \
    bash -c "\
        responds_with \
            \"Envoy single page app example\" \
            \"http://localhost:${PORT_DEV_PROXY}\""
run_log "Inititiate dev login (Github)"
responds_with_header \
    "HTTP/1.1 302 Found" \
    "http://localhost:${PORT_DEV_PROXY}/login"
responds_with_header \
    "location: https://github.com/login/oauth/authorize?client_id=XXX&redirect_uri=http%3A%2F%2Flocalhost%3A${PORT_DEV_PROXY}%2Fauthorize&response_type=code&scope=user%3Aemail&state=http%3A%2F%2Flocalhost%3A${PORT_DEV_PROXY}%2Flogin" \
    "http://localhost:${PORT_DEV_PROXY}/login"

run_log "Test production app (Github)"
responds_with \
    "Envoy single page app example" \
    "https://localhost:${PORT_PROXY}" \
    -k
responds_with_header \
    "location: https://github.com/login/oauth/authorize?client_id=XXX&redirect_uri=https%3A%2F%2Flocalhost%3A${PORT_PROXY}%2Fauthorize&response_type=code&scope=user%3Aemail&state=https%3A%2F%2Flocalhost%3A${PORT_PROXY}%2Flogin" \
    "https://localhost:${PORT_PROXY}/login" \
    -k
