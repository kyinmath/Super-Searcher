#include "globalinfo.h"
#include <atomic>
#include "generic_ipc.h"

//sender position, then user read position, then memory start.
//each user has one receiver and one sender.
extern std::atomic<uint64_t>* receive_mem;
extern std::atomic<uint64_t>* send_mem;