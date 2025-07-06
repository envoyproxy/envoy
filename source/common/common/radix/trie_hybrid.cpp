//
// Created by Ashesh Vidyut on 22/03/25.
//

#include "trie_hybrid.hpp"

// Explicit template instantiations
template class TrieHybrid<std::string, std::string>;
template class TrieHybrid<std::string, int>;
template class TrieHybrid<std::string, double>;
template class TrieHybrid<std::vector<uint8_t>, std::string>; 