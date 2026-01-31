#include "spip_common.h"

std::counting_semaphore<8> g_git_sem{8}; 
volatile std::atomic<bool> g_interrupted{false};
