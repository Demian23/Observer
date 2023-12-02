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
	bool Filter(const REG_NOTIFY_CLASS& notification, PCUNICODE_STRING keyName);
private:
	enum{MaxFilters = 16};
	RegistryFilter filters[MaxFilters];
	USHORT filtersCount;
	FastMutex _mutex;
};
