#ifndef header
#define header

#include <mqueue.h>
#include <fcntl.h>
#include <iostream>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>
#include <string>
#include <mutex>
#include <vector>
#include <map>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <random>

#define size_of_message 1024

const char* CONTROL_Q = "/control_q";

#endif