#pragma once

#include "FastMutex.h"

template<typename T>
struct FullItem{
	LIST_ENTRY Entry;
	T Data;
};

class List final {
public: 
	void init(ULONG _maxCount);
	void addItem(LIST_ENTRY* item);
	LIST_ENTRY* removeItem();
private:
	LIST_ENTRY head;
	ULONG count;
	ULONG maxCount;
	FastMutex lock;
};
