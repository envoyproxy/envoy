# A trick to allow invoking this script in multiple contexts.
if [ -z ${MY_LOCATION+x} ]; then
  if [ -d "$0.runfiles/" ]; then
    MY_LOCATION="$0.runfiles/bazel_tools/tools/objc"
  else
    MY_LOCATION="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  fi
fi

OUTPUT_FILE=""
INPUT_FILE=""

echo flags-in "$@"

declare -a FLAGS
let IDX=0
while [[ -n "$1" ]]
do
  case "$1" in
    -o)
      shift
      OUTPUT_FILE="$1"
      FLAGS[$IDX]="-o"
      let IDX=IDX+1
      FLAGS[$IDX]="$OUTPUT_FILE"
      let IDX=IDX+1
      ;;
    -p) ;;
    -R) shift ;;
    -S)
      FLAGS[$IDX]="$1"
      let IDX=IDX+1
      ;;
    *)
      INPUT_FILE="$1"
      FLAGS[$IDX]="$INPUT_FILE"
      let IDX=IDX+1
      ;;
  esac
  shift
done

echo flags-out "${FLAGS[@]}"

"${MY_LOCATION}"/xcrunwrapper.sh strip "${FLAGS[@]}"
touch -am -r "$INPUT_FILE" "$OUTPUT_FILE"
