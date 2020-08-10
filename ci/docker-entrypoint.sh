#!/usr/bin/env sh
set -e

# if the first argument look like a parameter (i.e. start with '-'), run Envoy
if [ "${1#-}" != "$1" ]; then
	set -- envoy "$@"
fi

if [ "$1" = 'envoy' ]; then
	# set the log level if the $loglevel variable is set
	if [ -n "$loglevel" ]; then
		set -- "$@" --log-level "$loglevel"
	fi
fi

if [ "$ENVOY_UID" != "0" ]; then
    if [ -n "$ENVOY_UID" ]; then
	usermod -u "$ENVOY_UID" envoy
    fi
    if [ -n "$ENVOY_GID" ]; then
	groupmod -g "$ENVOY_GID" envoy
    fi
    # Ensure the envoy user is able to write to container logs
    chown envoy:envoy /dev/stdout /dev/stderr
    su-exec envoy "${@}"
else
    exec "${@}"
fi
