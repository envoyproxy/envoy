#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <forward_list>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <unordered_set>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop
