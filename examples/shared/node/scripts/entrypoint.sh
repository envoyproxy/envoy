#!/bin/bash -e

# Add local user
# Either use the LOCAL_USER_ID if passed in at runtime or
# fallback

DEFAULT_USER_NAME=node

USER_UID=${LOCAL_UID:-1000}
USER_NAME=${LOCAL_USER_NAME:-worker}
USER_HOME=${LOCAL_USER_HOME:-"/home/${USER_NAME}"}

echo "Starting (${*}) with user: $USER_UID $USER_NAME $USER_HOME"
usermod -l "$USER_NAME" -md "$USER_HOME"  "$DEFAULT_USER_NAME" || :
chown "$USER_NAME" "$USER_HOME"
export HOME="${USER_HOME}"
export PATH=$PATH:"${HOME}/.yarn/bin/"
mkdir -p ./dist
chown -R "$USER_NAME" ./dist
exec /usr/sbin/gosu "${USER_NAME}" "$@"
