#pragma once

class FastMutex final {
public:
	void init();
	void lock();
	void unlock();
private:
	FAST_MUTEX _mutex;
};
