#!/bin/bash
#
# Wrapper for 'bazel' that adds some shortcuts and directory-cache policy for
# different sorts of builds. This script expects CLANG_TOOLCHAIN to be set
# to the directory above "bin/clang".
#
# Bazel notes:
#     --subcommands
#     --runs_per_test=100
#     --config=clang-asan
#
# TODO(jmarantz): rewrite in Python.

# Use Clang for most builds.
export CC=$CLANG_TOOLCHAIN/bin/clang; export CXX=$CLANG_TOOLCHAIN/bin/clang++

# Find the top level of this git client, or die trying.
while [ ! -e .git ]; do
  if [ "$PWD" = "/" ]; then
    echo $0 must be run from inside a git client.
  fi
  cd ..
done

# Identify the git client relative to the cache's parent directory, cleaned
# up a little. For example, assuming the cache prefix "/home/user/.cache."
# and the git directory is "/home/user/git2", we'll make the cache
# prefix be: "/home/user/.cache.git2.
cache_directory="$HOME"
dir_above_this=$(realpath "$PWD/..")
client_id=$(echo $dir_above_this | sed -e "s@^$cache_directory/@@" |\
  sed -e s@/@.@)

# As the disk caches never GC, show what they are so users can self-GC. Note
# that though we specify --disk_cache=... for object files, bazel still appears
# to put huge amounts of stuff in ~/.cache so we look at that too.
(set -x; du -s -h $HOME/.cache $HOME/.cache.*)

# Assume fastbuild unless we see an overriding switch. We are going to manually
# direct the disk-cache to a directory based on the compilation-mode and the
# top of the client.
mode="fastbuild"
args=""

if [ "$1" == "-nocache" ]; then
  args="--cache_test_results=no $args"
  shift
fi

if [ "$1" == "-debug" ]; then
  # Use g++ compilation for debugging, as -- at least at Google -- gdb does
  # a better job showing string contents. It also helps catch some compilation
  # issues that clang doesn't, and by including it in the development flow
  # helps keep coverage builds clean, as the Envoy coverage build currently
  # also uses g++.
  unset CC
  unset CXX
  args="$args --compilation_mode=dbg"
  mode="debug"
  shift
elif [ "$1" == "-valgrind" ]; then
  # Use g++ for valgrind as well. This can share a disk cache with "-debug".
  unset CC
  unset CXX
  args="$args --compilation_mode=dbg $args \
    --run_under=`pwd`/tools/run-valgrind.sh --define=tcmalloc=disabled"
  mode="debug"
  shift
elif [ "$1" == "-opt" ]; then
  args="$args --compilation_mode=opt $args"
  mode="opt"
  shift
elif [ "$1" == "-optdebug" ]; then
  echo setting optdebug mode ...
  args="$args --compilation_mode=opt --cxxopt=-g --cxxopt=-ggdb3 $args"
  mode="optdebug"
  shift
elif [ "$1" == "-stacktrace" ]; then
  # Note: we compile for debug here, but with clang rather than g++, so
  # we don't share a cache.
  args="$args --compilation_mode=dbg $args  \
        --run_under=`pwd`/tools/stack_decode.py \
        --strategy=TestRunner=standalone --test_output=all"
  mode="stacktrace"
  shift
elif [ "$1" == "-msan" ]; then
  # Note: this doesn't currently work.
  args="$args --config=clang-msan $args"
  mode="msan"
  shift
elif [ "$1" == "-tsan" ]; then
  args="$args --config=clang-tsan $args"
  mode="tsan"
  shift
elif [ "$1" == "-asan" ]; then
  args="$args --config=clang-asan $args"
  mode="asan"
  shift
elif [ "$1" == "-dtsan" ]; then
  # If there are tsan errors, it can be helpful to see debug line numbers.
  args="$args --config=clang-tsan --compilation_mode=dbg $args"
  mode="dtsan"
  shift
elif [ "$1" == "-dasan" ]; then
  # If there are asan errors, it can be helpful to see debug line numbers.
  args="$args --config=clang-asan --compilation_mode=dbg $args"
  mode="dasan"
  shift
fi

command="$1"
shift

if [ "$command" = "coverage" ]; then
  mode="coverage"
fi

# synthesize a readable and unique cache directory, so it's easy to know
# what to manually delete.
args="$args --disk_cache=$cache_directory/.cache.$client_id.$mode"

# TODO(jmarantz): add filtering out of uninteresting error messages.
echo bazel "$command" $args "$@"
bazel "$command" $args "$@"
status=$?

echo "Status = $status"
exit $status
