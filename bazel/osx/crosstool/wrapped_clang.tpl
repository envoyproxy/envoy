# A trick to allow invoking this script in multiple contexts.
if [ -z ${MY_LOCATION+x} ]; then
  if [ -d "$0.runfiles/" ]; then
    MY_LOCATION="$0.runfiles/bazel_tools/tools/objc"
  else
    MY_LOCATION="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  fi
fi

"${MY_LOCATION}"/xcrunwrapper.sh clang $@
