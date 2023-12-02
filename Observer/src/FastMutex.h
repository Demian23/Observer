#pragma once

class FastMutex final {
public:
	inline void Init() noexcept
	{
		ExInitializeFastMutex(&_mutex);
	}
	inline void Lock() noexcept
	{
		ExAcquireFastMutex(&_mutex);
	}
	inline void Unlock() noexcept
	{
		ExReleaseFastMutex(&_mutex);
	}
private:
	FAST_MUTEX _mutex;
};
