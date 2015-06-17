#include <atomic>
#include "console.h"
class mutex_lock
{
	enum { UNLOCKED, LOCKED };
	std::atomic<unsigned> lock;
public:
	mutex_lock() : lock(UNLOCKED) {}
	bool attempt_grab()
	{
		return lock.exchange(LOCKED, std::memory_order_acquire) == UNLOCKED;
	}
	void guaranteed_grab()
	{
		while (lock.exchange(LOCKED, std::memory_order_acquire) == LOCKED)
		{
		}
		return;
	}
	void release()
	{
		lock.store(UNLOCKED, std::memory_order_release);
	}
};


class rw_lock
{
	std::atomic<uint64_t> lock;
public:
	rw_lock(uint64_t starting_state = 0) : lock(starting_state) {}
	void grab_read()
	{
		//we have a maximum number of speculative read attempts that can happen simultaneously: the # of threads
		//we require there to be less than 2^20 threads.
		//then, we can add at most # of threads speculatively to the value. This keeps it below the admin lock number.
		while (lock.fetch_add(1) > 429496729)
		{
			lock.fetch_sub(1);
		}
	}
	void release_read()
	{
		lock.fetch_sub(1);
	}

	bool try_write()
	{
		if (lock.fetch_add(42949672960ULL) != 0)
		{
			lock.fetch_sub(42949672960ULL);
			return 0;
		}
		else return 1;
	}
	void release_write()
	{
		lock.fetch_sub(42949672960ULL);
	}
};