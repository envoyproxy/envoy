#!/usr/bin/python

# Launcher for generating composite serializer code.

import source.extensions.filters.network.kafka.serialization_code_generator.serialization_composite_generator as generator

def main():
    print 'Generating (main) serialization files'
    generator.generate_files()

if __name__ == "__main__":
  main()
