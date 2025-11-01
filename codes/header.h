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
#include <map>         // สำหรับเก็บข้อมูลห้อง
#include <algorithm>   // สำหรับค้นหาและลบ

#define size_of_message 1024

const char* CONTROL_Q = "/control_q";

#endif