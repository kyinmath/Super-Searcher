#include "comms.h"
uint64_t mmap_size;
std::atomic<uint64_t>* receive_mem;
std::atomic<uint64_t>* send_mem;