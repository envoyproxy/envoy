#pragma once

#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <netdb.h>
#include <netinet/tcp.h>
#include <regex>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <unordered_set>
#include <random>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop
