#include <map>
#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <climits>

template <typename Type>
class Lattice {
public:
  using Coordinate = std::vector<std::string>;

  Lattice(std::vector<std::string> dimension_names)
      : dimension_names_(dimension_names) {
    for (std::string dimension : dimension_names) {
      values_by_dimension_[dimension] = std::vector<std::string>();
    }
  }

  void remove_endpoints_for_sector(Coordinate sector_coordinates, std::vector<Type> endpoints) {
    auto& ep = endpoints_by_coordinate_[sector_coordinates];

    for (auto& endpoint : endpoints)
      ep.erase(std::remove(ep.begin(), ep.end(), endpoint), ep.end());

    if (!ep.size()) {
      // todo
    }
  }

  void add_endpoints_for_sector(Coordinate sector_coordinates, std::vector<Type> endpoints) {
    // Add coordinate value if it's not already present
    if(!endpoints_by_coordinate_[sector_coordinates].size())
      for (uint i = 0; i < dimension_names_.size(); i++)
        values_by_dimension_[dimension_names_[i]].push_back(sector_coordinates[i]);

    endpoints_by_coordinate_[sector_coordinates].insert(
      endpoints_by_coordinate_[sector_coordinates].end(),
      endpoints.begin(),
      endpoints.end()
    );
  }

  std::vector<std::string> get_dimension_names() {
    return dimension_names_;
  }

  std::vector<Type> get_endpoints_for_sector(Coordinate sector_coordinates) {
    return endpoints_by_coordinate_[sector_coordinates];
  }

  std::vector<std::string> get_dimension_values(std::string dimension_name) {
    return values_by_dimension_[dimension_name];
  }

  std::map<std::string, int> get_dimensionality() {
    std::map<std::string, int> dimensionality;
    for (auto dimension : dimension_names_) {
      dimensionality[dimension] = get_dimension_values(dimension).size();
    }
    return dimensionality;
  }

  void print_values_by_dimension() {
    std::cout << "Values By Dimension:" << std::endl;
    for (auto it = values_by_dimension_.begin(); it != values_by_dimension_.end(); it++) {
      std::cout << it->first << ": ";
      for (auto s : it->second) {
        std::cout << s << ", ";
      }
      std::cout << std::endl;
    }
  }

  void print_endpoints_by_coordinate() {
    std::cout << "Endpoints by Coordinate:" << std::endl;
    for (auto it = endpoints_by_coordinate_.begin(); it != endpoints_by_coordinate_.end(); it++) {
      std::cout << "Coordinate(";
      for (auto c : it->first) std::cout << c << ", ";
      std::cout << "): ";
      for (auto s : it->second) std::cout << s << ", ";
      std::cout << std::endl;
    }
  }

  std::vector<Coordinate> get_coordinates() {
    std::vector<Coordinate> v;
    for( auto it = endpoints_by_coordinate_.begin(); it != endpoints_by_coordinate_.end(); ++it ) {
        v.push_back(it->first);
    }
    return v;
  }

  std::vector<Type> get_endpoints() {
    std::vector<Type> v;
    for( auto it = endpoints_by_coordinate_.begin(); it != endpoints_by_coordinate_.end(); ++it )
      v.insert(v.end(), it->second.begin(), it->second.end());
    return v;
  }

  const std::vector<std::string> dimension_names_;
  std::map<std::string, std::vector<std::string> > values_by_dimension_;
  std::map<Coordinate, std::vector<Type>> endpoints_by_coordinate_;
};

template <typename Type>
class ShuffleSharder {
public:
  ShuffleSharder(uint64_t seed) : seed_(seed) { }

  Lattice<Type>* shuffleShard(Lattice<Type> lattice, uint64_t hash, unsigned long endpoints_per_cell) {
    Lattice<Type> * chosen = new Lattice<Type>(lattice.get_dimension_names());

    std::vector<std::vector<std::string>> shuffled_dimension_values;
    std::mt19937 g(seed_ + hash);

    for (std::string dimensionName : lattice.get_dimension_names()) {
      std::vector<std::string> shuffled_values = lattice.get_dimension_values(dimensionName);
      std::shuffle(shuffled_values.begin(), shuffled_values.end(), g);
      shuffled_dimension_values.push_back(shuffled_values);
    }

    auto dimensionality = lattice.get_dimensionality();

    if (dimensionality.size() == 1) {
      for (auto dimension_value : shuffled_dimension_values[0]) {
        std::vector<std::string> c{dimension_value};
        auto available_endpoints = lattice.get_endpoints_for_sector(c);
        std::shuffle(available_endpoints.begin(), available_endpoints.end(), g);
        std::vector<Type> returned_endpoints(available_endpoints.begin(), available_endpoints.begin() + std::min(endpoints_per_cell, available_endpoints.size()));
        chosen->add_endpoints_for_sector(c, returned_endpoints);
      }
      return chosen;
    }

    int minimum_dimension_size = INT_MAX;
    for (auto it = dimensionality.begin(); it != dimensionality.end(); it++) {
      if (it->second < minimum_dimension_size) {
        minimum_dimension_size = it->second;
      }
    }

    for (auto coordinates : lattice.get_coordinates()) {
      auto available_endpoints = lattice.get_endpoints_for_sector(coordinates);
      if (available_endpoints.size()) {
        std::shuffle(available_endpoints.begin(), available_endpoints.end(), g);
        std::vector<Type> returned_endpoints(available_endpoints.begin(), available_endpoints.begin() + std::min(endpoints_per_cell, available_endpoints.size()));
        chosen->add_endpoints_for_sector(coordinates, returned_endpoints);
      }
    }

    return chosen;
  }

  const uint64_t seed_;
};
