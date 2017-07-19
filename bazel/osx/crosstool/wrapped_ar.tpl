# A trick to allow invoking this script in multiple contexts.
if [ -z ${MY_LOCATION+x} ]; then
  if [ -d "$0.runfiles/" ]; then
    MY_LOCATION="$0.runfiles/bazel_tools/tools/objc"
  else
    MY_LOCATION="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  fi
fi

ARCHIVE_NAME=""
for arg in "$@"; do
  [[ "$ARCHIVE_NAME" = "" ]] && [[ "$arg" = *.* ]] && ARCHIVE_NAME="$arg"
done

# Prevents timestamp metadata being present in the archive contents.
export ZERO_AR_DATE=1

"${MY_LOCATION}"/xcrunwrapper.sh ar "$@"
# Setting ZERO_AR_DATE for the above invocation means the output will have
# zero timestamp, which, as an input, would break ld. Thus, update timestamp.
touch "$ARCHIVE_NAME"
