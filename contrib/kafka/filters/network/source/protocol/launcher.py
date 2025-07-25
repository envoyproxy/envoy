#!/usr/bin/python

# Launcher for generating Kafka protocol code.

import contrib.kafka.filters.network.source.protocol.generator as generator
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
                 'requests.cc'/'responses.cc',
                 'kafka_request_resolver.cc'/'kafka_response_resolver.cc',
                 and 'request_metrics.h'/'response_metrics.h'.
  INPUT_FILES: Kafka protocol json files to be processed.

  Kafka spec files are provided in Kafka clients jar file.

  Files created are:
    - ${MESSAGE_TYPE}s.h - definition of all the structures/deserializers/parsers related to Kafka
      requests/responses,
    - ${MESSAGE_TYPE}s.cc - implementation of all the functions in the file above,
    - kafka_${MESSAGE_TYPE}_resolver.cc - resolver that is responsible for creation of parsers
      defined in ${MESSAGE_TYPE}s.h (it maps request's api key & version to matching parser),
    - ${MESSAGE_TYPE}_metrics.h - rich metrics wrappers for all possible message types.

  Templates used are:
  - to create '${MESSAGE_TYPE}.h': ${MESSAGE_TYPE}_h.j2, complex_type_h.j2, complex_type_cc.j2,
    request_parser.j2,
  - to create '${MESSAGE_TYPE}.cc': ${MESSAGE_TYPE}_cc.j2, complex_type_h.j2, complex_type_cc.j2,
    request_parser.j2,
  - to create 'kafka_${MESSAGE_TYPE}_resolver.cc': kafka_${MESSAGE_TYPE}_resolver_cc.j2,
  - to create '${MESSAGE_TYPE}_metrics.h': ${MESSAGE_TYPE}_metrics_h.j2.
  """

    type = sys.argv[1]
    main_h_file = os.path.abspath(sys.argv[2])
    main_cc_file = os.path.abspath(sys.argv[3])
    resolver_cc_file = os.path.abspath(sys.argv[4])
    metrics_h_file = os.path.abspath(sys.argv[5])
    input_files = sys.argv[6:]
    generator.generate_main_code(
        type, main_h_file, main_cc_file, resolver_cc_file, metrics_h_file, input_files)


if __name__ == "__main__":
    main()
