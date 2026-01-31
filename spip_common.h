#pragma once
#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <format>
#include <functional>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>
#include <regex>
#include <set>
#include <map>
#include <chrono>
#include <string_view>
#include <sys/resource.h>
#include <csignal>
#include <sqlite3.h>
#include <future>
#include <semaphore>

namespace fs = std::filesystem;
extern std::counting_semaphore<8> g_git_sem;
extern volatile std::atomic<bool> g_interrupted;
