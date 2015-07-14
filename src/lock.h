#include <atomic>
#include "globalinfo.h"
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


//Lo = Lockable
template<class T>
struct Lo : private T
{
	using T::T;
	//void perm_lock() {lock.guaranteed_grab();}
	//the type held here is only valid for as long as the lock is alive. don't try to extract x unless you know the lock will be alive for longer than that.
	//don't call get_interior except for very short periods of time! it's not meant to be kept locked.
	struct holder
	{
		T* x;
		rw_lock& lock;
		holder(T* t, rw_lock& l) : x(t), lock(l) {}
		~holder() { lock.release_read(); };
	};
	rw_lock lock;
	holder get_read()
	{
		lock.grab_read();
		return holder((T*)this, lock);
	}

	T* bypass()
	{
		return (T*)this;
	}
};