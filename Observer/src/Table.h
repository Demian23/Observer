#pragma once

#include "FastMutex.h"

using Comparator = RTL_GENERIC_COMPARE_RESULTS (*)(PRTL_GENERIC_TABLE, PVOID, PVOID);

template<typename T, typename K, Comparator C>
class Table final {
public:
	void Init()
	{
		RtlInitializeGenericTable(&table, C, Allocate, Deallocate, nullptr);
		mutex.Init();
	}
	bool IsKeyPresented(K key)
	{
		T search{ key };
		return RtlLookupElementGenericTable(&table, &search) != nullptr;
	}
	bool Insert(T* element, CLONG size)
	{
		AutoLock lock(mutex);	
		bool result = false;
		auto existingItem = RtlLookupElementGenericTable(&table, element);
		if (existingItem == nullptr) {
			existingItem = RtlInsertElementGenericTable(&table, element, size, nullptr);
			result = existingItem != nullptr;
		}
		// TODO if not equal info update 
		return result;
	}
	bool Delete(K key) {
		AutoLock lock(mutex);T data{ key };
		return RtlDeleteElementGenericTable(&table, &data);
	}
	T* Get(K key)
	{
		AutoLock lock(mutex);	
		T data{ key };
		return static_cast<T*>(RtlLookupElementGenericTable(&table, &data));
	}
	void Dispose()
	{
		PVOID element;
		while ((element = RtlGetElementGenericTable(&table, 0)) != nullptr) {
			RtlDeleteElementGenericTable(&table, element);
		}
	}
private:
	inline static void Deallocate(PRTL_GENERIC_TABLE, PVOID buffer){ExFreePool(buffer);}
	inline static PVOID Allocate(PRTL_GENERIC_TABLE, CLONG bytes){
		return ExAllocatePool2(POOL_FLAG_PAGED | POOL_FLAG_UNINITIALIZED, bytes, DRIVER_TAG);
	}
	
	RTL_GENERIC_TABLE table;
	FastMutex mutex;
};
