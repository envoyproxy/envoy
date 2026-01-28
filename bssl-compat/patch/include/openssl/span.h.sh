#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-regex 'template <typename [TC]>' \
  --uncomment-regex 'template <typename C, typename T>' \
  --uncomment-regex 'template <' \
  --uncomment-regex 'inline constexpr.*' \
  --uncomment-regex '    typename\s[TC=].*' \
  --uncomment-class-fwd Span \
  --uncomment-regex-range 'namespace internal {' '};'\
  --uncomment-regex-range 'using EnableIfContainer' '.\s\s..\snamespace\sinternal'\
  --uncomment-class Span \
  --uncomment-regex 'Span(.*' \
  --uncomment-regex '.*Span<T>::npos' \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl StringAsBytes \
  --uncomment-func-impl BytesAsStringView \
