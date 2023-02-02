#!/usr/bin/env bash

set -e

if [[ $(id -u envoybuild) != "${BUILD_UID}" ]]; then
    usermod -u "${BUILD_UID}" envoybuild
    chown envoybuild /home/envoybuild
    chown envoybuild /output
fi

exec gosu envoybuild "$@"
