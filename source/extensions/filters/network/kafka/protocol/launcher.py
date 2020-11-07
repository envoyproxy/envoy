#!/usr/bin/python

# Launcher for generating Kafka protocol code.

import source.extensions.filters.network.kafka.protocol.generator as generator
import sys
import os


def main():
  """
  Kafka code generator script
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Generates C++ code from Kafka protocol specification for Kafka codec.

  Usage:
    launcher.py MESSAGE_TYPE OUTPUT_FILES INPUT_FILES
  where:
  MESSAGE_TYPE : 'request' or 'response'
  OUTPUT_FILES : location of 'requests.h'/'responses.h',
                 'kafka_request_resolver.cc'/'kafka_response_resolver.cc' and
                 'request_metrics.h'/'response_metrics.h'.
  INPUT_FILES: Kafka protocol json files to be processed.

  Kafka spec files are provided in Kafka clients jar file.

  Files created are:
    - ${MESSAGE_TYPE}s.h - definition of all the structures/deserializers/parsers related to Kafka
      requests/responses,
    - kafka_${MESSAGE_TYPE}_resolver.cc - resolver that is responsible for creation of parsers
      defined in ${MESSAGE_TYPE}s.h (it maps request's api key & version to matching parser),
    - ${MESSAGE_TYPE}_metrics.h - rich metrics wrappers for all possible message types.

  Templates used are:
  - to create '${MESSAGE_TYPE}.h': ${MESSAGE_TYPE}_h.j2, complex_type_template.j2,
    request_parser.j2,
  - to create 'kafka_${MESSAGE_TYPE}_resolver.cc': kafka_${MESSAGE_TYPE}_resolver_cc.j2,
  - to create '${MESSAGE_TYPE}_metrics.h': ${MESSAGE_TYPE}_metrics_h.j2.
  """

  type = sys.argv[1]
  main_header_file = os.path.abspath(sys.argv[2])
  resolver_cc_file = os.path.abspath(sys.argv[3])
  metrics_h_file = os.path.abspath(sys.argv[4])
  input_files = sys.argv[5:]
  generator.generate_main_code(type, main_header_file, resolver_cc_file, metrics_h_file,
                               input_files)


if __name__ == "__main__":
  main()
