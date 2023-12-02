#pragma once

template<typename T>
struct AutoLock final{
	AutoLock(T& lock) :_lock(lock) { _lock.Lock(); }
	~AutoLock() { _lock.Unlock(); }
private:
	T& _lock;
};
