#pragma once

class Mutex final {
public:
	inline void Init() { KeInitializeMutex(&mutex, 0); };
	inline void Lock() { KeWaitForSingleObject(&mutex, Executive, KernelMode, FALSE, nullptr); };
	inline void Unlock() { KeReleaseMutex(&mutex, FALSE); };
private:
	KMUTEX mutex;
};
