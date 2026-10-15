#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --uncomment-regex '    true' \
  --uncomment-regex 'template <typename [TC]>' \
  --uncomment-regex 'template <typename C, typename T>' \
  --uncomment-regex 'template <' \
  --uncomment-regex 'inline constexpr.*' \
  --uncomment-regex '    typename\s[TC=].*' \
  --uncomment-class-fwd Span \
  --uncomment-regex-range 'namespace internal {' '};'\
  --uncomment-regex-range 'using EnableIfContainer' '    std::is_integral_v'\
  --uncomment-regex 'struct AllowRedeclaringConstructor' \
  --uncomment-regex '.\s\s..\snamespace\sinternal'\
  --uncomment-class SpanStorage \
  --uncomment-class SpanStorage \
  --uncomment-class Span \
  --uncomment-regex 'Span(.*' \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl MakeConstSpan \
  --uncomment-func-impl StringAsBytes \
  --uncomment-func-impl BytesAsStringView \
