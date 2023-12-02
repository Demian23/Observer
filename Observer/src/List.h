#pragma once

#include "FastMutex.h"

template<typename T>
struct FullItem{
	LIST_ENTRY Entry;
	T Data;
};

class List final {
public: 
	void Init(ULONG _maxCount);
	void AddItem(LIST_ENTRY* item);
	LIST_ENTRY* RemoveItem();
private:
	LIST_ENTRY head;
	ULONG count;
	ULONG maxCount;
	FastMutex lock;
};
