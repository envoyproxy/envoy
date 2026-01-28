#!/bin/bash

set -euo pipefail

HDR_FILE="${1?"HDR_FILE not specified"}"
shift

function info {
  true || echo "INFO[$HDR_FILE]: $1"
}

function warn {
  echo "WARN[$HDR_FILE]: $1"
}

function error {
  echo "ERROR[$HDR_FILE]: $1"
  exit 1
}

cleanup() {
  if [[ $? != 0 ]]; then
    echo "ERROR[$HDR_FILE]: Error while processing option \"$OPTION\""
  fi
}
trap cleanup EXIT

[[ -f "$HDR_FILE" ]] || error "HDR_FILE $HDR_FILE does not exist"


# This contains the current command line option being processed. It is used
# when reporting low level errors, to provide some context for the error
OPTION="<NONE>"
option_start() {
  OPTION=$1
}
option_add() {
  while [[ $# -gt 0 ]]; do
    OPTION="$OPTION '$1'"
    shift
  done
}
option_end() {
  option_add "$@"
  info "  $OPTION"
}

run_sed_expression() {
  [[ $# == 1 ]] || error "run_sed_expression(): One argument required"
  local SED_EXPRESSION="$1"
  sed -i.bak -e "$SED_EXPRESSION" "$HDR_FILE"
  if ! cmp -s "$HDR_FILE" "$HDR_FILE.bak"; then
    ((CHANGES += 1))
    if [[ -v MELD ]]; then
      meld "$HDR_FILE.bak" "$HDR_FILE"
    fi
  fi
  rm -f "$HDR_FILE.bak"
}

uncomment_line_range() {
  [[ $# == 2 ]] || error "uncomment_line_range(): Two line numbers required"
  run_sed_expression "${1},${2}s%^// %%g"
}

uncomment_regex_range() {
  [[ $# == 2 ]] || error "uncomment_regex_range(): Two regexes required"
  L1=$(grep -n "^// $1" "$HDR_FILE" | sed -n 1p | cut -d: -f1)
  L2=$(grep -n "^// $2" "$HDR_FILE" | awk -F: '$1 > '"$L1"' {print $1; exit}')
  [ -z "$L1" ] && error "Failed to locate first pattern '$1'"
  [ -z "$L2" ] && error "Failed to locate second pattern '$2'"
  uncomment_line_range "$L1" "$L2"
}

uncomment_preproc_directive() {
  [[ $# == 1 ]] || error "uncomment_preproc_directive(): One regex required"
  while read -r L1; do
    L2=$(awk '(NR == '"$L1"'),/[^\\]$/{print NR}' "$HDR_FILE" | tail -1)
    uncomment_line_range "$L1" "$L2"
  done < <(grep -n "^// #\s*$1" "$HDR_FILE" | cut -d: -f1)
}

comment_line_range() {
  [[ $# == 2 ]] || error "comment_line_range(): Two line numbers required"
  run_sed_expression "${1},${2}s%^%//%g"
}

comment_regex_range() {
  [[ $# == 2 ]] || error "comment_regex_range(): Two regexes required"
  L1=$(grep -n "$1" "$HDR_FILE" | sed -n 1p | cut -d: -f1)
  L2=$(awk '(NR == '"$L1"'),/'"$2"'/{print NR}' "$HDR_FILE" | tail -1)
  [ -z "$L1" ] && error "comment_regex_range(): Failed to locate first pattern"
  [ -z "$L2" ] && error "comment_regex_range(): Failed to locate second pattern"
  comment_line_range "$L1" "$L2"
}


while [ $# -ne 0 ]; do
  option_start "$1"
  CHANGES=0
  case "$1" in
    --comment) # Comment everything out
      option_end
      run_sed_expression 's|^|// |'
      run_sed_expression 's|^// $||'
      run_sed_expression 's|^// //|//|'
      run_sed_expression 's|^// /\*|/*|'
      run_sed_expression 's|^//  \*$| *|'
      run_sed_expression 's|^//  \* | * |'
      run_sed_expression 's|^//  \*/$| */|'
    ;;
    "-h") # Set of general stuff like include guards, extern, and #if/else/end
      option_end
      for DIRECTIVE in include if ifdef ifndef else elif endif undef error warning; do
        uncomment_preproc_directive "\<$DIRECTIVE\>"
      done
      uncomment_preproc_directive "define\s*OPENSSL_HEADER_.*"
      run_sed_expression "s%^// \(extern\s*\"C+\?+\?\".*\)$%\1%g"
      run_sed_expression "s%^// \(}\s*/[/\*]\s*extern\s*\"\?C+\?+\?\"\?.*\)$%\1%g"
      run_sed_expression "s%^// \(BSSL_NAMESPACE_\(BEGIN\|END\)\)$%\1%g"
    ;;
    --uncomment-func-decl) # Function name
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      FUNC_SIG_MULTI_LINE="$(grep -Pzob "OPENSSL_EXPORT\s*[^;]*[^A-Za-z0-9_]$2\s*\([^;]*\)" "$HDR_FILE" | sed -e 's/OPENSSL_EXPORT\s*//g' -e 's%^// %%' -e 's/\x0//g')"
      FUNC_SIG_LINE_COUNT="$(echo "$FUNC_SIG_MULTI_LINE" | wc -l)"
      FUNC_SIG_OFFSET="$(echo "$FUNC_SIG_MULTI_LINE" | grep -o '^[0-9]*:' | cut -d: -f1)"
      FUNC_SIG_LINE_FROM=$(($(head -c+"$FUNC_SIG_OFFSET" "$HDR_FILE" | wc -l) + 1))
      FUNC_SIG_LINE_TO=$((FUNC_SIG_LINE_FROM + FUNC_SIG_LINE_COUNT - 1))
      uncomment_line_range ${FUNC_SIG_LINE_FROM} ${FUNC_SIG_LINE_TO}
      shift
    ;;
    --uncomment-regex) # Uncomment consecutive lines matching regexes
      PATTERNS=()
      while [[ $# -gt 1 ]] && [[ $2 != -* ]]; do
        shift && PATTERNS[${#PATTERNS[@]}]="$1"
        option_add "$1"
      done
      option_end
      if [[ ${#PATTERNS[@]} == 1 ]]; then
        run_sed_expression "s%^// \(${PATTERNS[0]}\)%\1%"
      else
        while read -r L1; do
          for ((i=1; i < ${#PATTERNS[@]} ; i++)); do
            L2=$((L1 + i))
            if grep -n "^// ${PATTERNS[i]}" "$HDR_FILE" | cut -d: -f1 | grep -q "^$L2$"; then
              if (( i == ${#PATTERNS[@]} - 1 )); then
                uncomment_line_range "$L1" "$L2"
                break 2 # exit both loops
              fi
            fi
          done
        done < <(grep -n "^// ${PATTERNS[0]}" "$HDR_FILE" | cut -d: -f1)
      fi
    ;;
    --uncomment-macro-redef) # -d Redefine macro <X> to be ossl_<x>
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      run_sed_expression "s%^//\(\s\)#\s*define\s*\<\($2\)\>.*$%#ifdef\1ossl_\2\n#define\1\2\1ossl_\2\n#endif%"
      shift
    ;;
    --uncomment-typedef-redef) # -t Redefine "typedef struct <Y> <X>" to be "typedef struct ossl_<Y> <X>"
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      run_sed_expression "s%^//\(\s*\)typedef\s*struct\s*\([[:alnum:]_]*\)\s*\(\<$2\>\);%typedef\1struct\1ossl_\2\1\3;%"
      shift
    ;;
    --uncomment-macro) # Uncomment #define <X>.... (including continuation lines)
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      while read -r L1; do
        L2=$(awk '(NR == '"$L1"'),/[^\\]$/{print NR}' "$HDR_FILE" | tail -1)
        uncomment_line_range "$L1" "$L2"
      done < <(grep -n "^// #\s*define\s*$2\>" "$HDR_FILE" | cut -d: -f1)
      shift
    ;;
    --uncomment-regex-range) # Uncomment multi-line matching regex
      [[ $3 ]] && [[ $2 != -* ]] && [[ $3 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2" "$3"
      uncomment_regex_range "$2" "$3"
      shift 2
    ;;
    --uncomment-gtest-func)
      [[ $3 ]] && [[ $2 != -* ]] && [[ $3 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2" "$3"
      uncomment_regex_range "\s*\(TEST\|TEST_P\)\s*($2\s*,\s*$3\s*)\s*{" "}"
      shift 2
    ;;
    --uncomment-gtest-func-skip)
      [[ $3 ]] && [[ $2 != -* ]] && [[ $3 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2" "$3"
      uncomment_regex_range "\(TEST\|TEST_P\)\s*($2\s*,\s*$3\s*)\s*{" "}"
      run_sed_expression '/^\(TEST\|TEST_P\)\s*(\s*'"$2"'\s*,\s*'"$3"'\s*)\s*{/a #ifdef BSSL_COMPAT\nGTEST_SKIP() << "TODO: Investigate failure on BSSL_COMPAT";\n#endif'
      shift 2
    ;;
    --uncomment-struct) # Uncomment struct
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      uncomment_regex_range "struct\s*$2\>.*{$" "}.*;$"
      shift
    ;;
    --uncomment-class-fwd) # Uncomment class forward decl
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      run_sed_expression "s%^// \(class\s*$2\s*;\s*\)$%\1%"
      shift
    ;;
    --uncomment-class) # Uncomment class
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      uncomment_regex_range "class\s*\<$2\>" "};$"
      shift
    ;;
    --uncomment-enum) # Uncomment enum
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      uncomment_regex_range "enum\s*\<$2\>" "};$"
      shift
    ;;
    --uncomment-typedef) # Uncomment typedef
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      LINE=$(grep -n "^// \s*\<typedef\>.*\<$2\>.*" "$HDR_FILE" | sed -n 1p)
      L1=$(echo "$LINE" | cut -d: -f1) && L2=$L1
      if [[ ! "$LINE" =~ \;$ ]]; then # multi-line
        L2=$(awk '(NR == '"$L1"'),/^\/\/ .*;$/{print NR}' "$HDR_FILE" | tail -1)
      fi
      uncomment_line_range "$L1" "$L2"
      shift
    ;;
    --uncomment-func-impl)
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      LINE=$(grep -n "^// [^ !].*\b$2\s*(.*[^;]$" "$HDR_FILE" | sed -n 1p)
      L1=$(echo "$LINE" | cut -d: -f1) && L2=$L1
      if [[ ! "$LINE" =~ }$ ]]; then # multi-line
        L2=$(awk '(NR == '"$L1"'),/^\/\/ }$/{print NR}' "$HDR_FILE" | tail -1)
      fi
      uncomment_line_range "$L1" "$L2"
      shift
    ;;
    --uncomment-static-func-impl)
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      LINE=$(grep -n "^// static\s*.*\b$2\b\s*(" "$HDR_FILE" | sed -n 1p)
      L1=$(echo "$LINE" | cut -d: -f1) && L2=$L1
      if [[ ! "$LINE" =~ }$ ]]; then # multi-line
        L2=$(awk '(NR == '"$L1"'),/^\/\/ }$/{print NR}' "$HDR_FILE" | tail -1)
      fi
      uncomment_line_range "$L1" "$L2"
      shift
    ;;
    --uncomment-using)
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      LINE=$(grep -n "^// \s*\<using\>.*\<$2\>.*" "$HDR_FILE" | sed -n 1p)
      L1=$(echo "$LINE" | cut -d: -f1) && L2=$L1
      if [[ ! "$LINE" =~ \;$ ]]; then # multi-line
        L2=$(awk '(NR == '"$L1"'),/^\/\/ .*;$/{print NR}' "$HDR_FILE" | tail -1)
      fi
      uncomment_line_range "$L1" "$L2"
      shift
    ;;
    --comment-regex-range) # comment multi-line matching regex
      [[ $3 ]] && [[ $2 != -* ]] && [[ $3 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2" "$3"
      comment_regex_range "$2" "$3"
      shift 2
    ;;
    --comment-gtest-func)
      [[ $3 ]] && [[ $2 != -* ]] && [[ $3 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2" "$3"
      comment_regex_range "^\s*\(TEST\|TEST_P\)\s*($2\s*,\s*$3\s*)\s*{" "^}"
      shift 2
    ;;
    --comment-regex)
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      run_sed_expression "s%\($2\)%// \1%"
      shift
    ;;
    "--sed") # sed expression
      [[ $2 ]] && [[ $2 != -* ]] || error "Insufficient arguments for $1"
      option_end "$2"
      run_sed_expression "$2"
      shift
    ;;
    --echo-on)
      set -x
    ;;
    --echo-off)
      set +x
    ;;
    --meld-on)
      MELD=1
    ;;
    --meld-off)
      unset MELD
    ;;
    *)
      error "Unknown option $1"
    ;;
  esac
  if (( CHANGES == 0 )); then
    error "No changes were made"
  fi
  shift
done


