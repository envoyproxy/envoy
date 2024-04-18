#!/bin/bash -eu

set -o pipefail

# The key configured here can either be the snakeoil key created
# in CI, or in PostSubmit its the actual maintainer key downloaded
# from an azp SecureFile

# If the intended destination of the `.gnupg` file is not the same
# as where it is configured (ie configuring on the host for use in a
# container), GNUPGHOME_CONFIGURED can be set pointing to where `.gnupg`
# will be.

GNUPGHOME="${GNUPGHOME:-${HOME}/.gnupg}"
GPG_PASSPHRASE="${GPG_PASSPHRASE:-HACKME}"
GPG_KEYFILE="${GPG_KEYFILE:-test.env/ci.snakeoil.gpg.key}"

# In the case of non-PostSubmit AZP leaves the `secureFilePath` variable
# uniterpolated, so matching here is matching that its not set.
#
# shellcheck disable=SC2016
if [[ "$GPG_KEYFILE" == '$(MaintainerGPGKey.secureFilePath)' ]]; then
    GPG_PASSPHRASE=HACKME
    GPG_KEYFILE="test.env/ci.snakeoil.gpg.key"
fi

# The configured home, may be different if the configuration
# is to be used inside a container or different env to the one
# in which the configuration is created.
if [[ -z "$GNUPGHOME_CONFIGURED" ]]; then
    GNUPGHOME_CONFIGURED="$GNUPGHOME"
fi

if [[ ! -e "$GNUPGHOME" ]]; then
    mkdir -p "$GNUPGHOME"
    chmod 700 "${GNUPGHOME}"
fi

# Reload the gpg-agent
eval "$(gpg-agent --daemon --allow-loopback-pinentry)"

# Load the key
echo "$GPG_PASSPHRASE" | gpg --batch --yes --passphrase-fd 0 --import "$GPG_KEYFILE"

# Set the passphrase in a file
echo "$GPG_PASSPHRASE"  > "${GNUPGHOME}/gpg-passphrase"
chmod 600 "${GNUPGHOME}/gpg-passphrase"

# Configure gpg to use the file - not the configured path may be different
{
    echo "use-agent"
    echo "pinentry-mode loopback"
    echo "passphrase-file ${GNUPGHOME_CONFIGURED}/gpg-passphrase"
} >> "$GNUPGHOME/gpg.conf"

# Configure gpg-agent
echo "allow-loopback-pinentry" >> "$GNUPGHOME/gpg-agent.conf"
echo RELOADAGENT | gpg-connect-agent

if [[ "$GNUPGHOME" != "$GNUPGHOME_CONFIGURED" ]]; then
    echo "GPG configured in ${GNUPGHOME} for ${GNUPGHOME_CONFIGURED}"
else
    echo "GPG configured in ${GNUPGHOME}"
fi
