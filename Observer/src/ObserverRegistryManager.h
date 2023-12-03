#pragma once
// class that manage registry setting and filters for this driver

#include "FastMutex.h"
#include "../../Client/src/ObserverClientInfo.h"

// it should filter not only on registry name, but on registry notification type too
class ObserverRegistryManager final {
public:

struct RegistryFilter final {
	USHORT allowedNotifications;
	UNICODE_STRING registryRootName;
};

	void Init();
	void RemoveAllFilters();
	int FindFilter(PCWSTR filteredRootName);
	bool RemoveFilter(PCWSTR filteredRootName);
	NTSTATUS AddFilter(const ClientRegistryFilter* filter);
	bool AddFilterFromKernel(const RegistryFilter filter);
	bool Filter(const REG_NOTIFY_CLASS& notification, PCUNICODE_STRING keyName);
private:
	enum{MaxFilters = 16};
	RegistryFilter filters[MaxFilters];
	USHORT filtersCount;
	FastMutex _mutex;

	bool RemoveFilter(int index);
	inline int FindFreeIndex()const
	{
		for (short index = 0; index < MaxFilters; index++)
			if (!filters[index].allowedNotifications)
				return index;
		return -1;
	}
	inline int FindExistingFilterIndex(PCUNICODE_STRING keyName)
	{
		for (short i = 0; i < MaxFilters; i++) {
			if (filters[i].allowedNotifications &&
				RtlEqualUnicodeString(keyName, &filters[i].registryRootName, TRUE)) {
				return i;
			}
		}
		return -1;
	}
};
