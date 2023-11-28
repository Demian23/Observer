#pragma once

template<typename T>
struct AutoLock {
	AutoLock(T& lock) :_lock(lock) { _lock.lock(); }
	~AutoLock() { _lock.unlock(); }
private:
	T& _lock;
};
