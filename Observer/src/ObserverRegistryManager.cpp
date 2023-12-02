#include "pch.h"
#include "AutoLock.h"
#include "Observer.h"
#include "ObserverRegistryManager.h"

void ObserverRegistryManager::Init()
{
	_mutex.Init();
	for(short i = 0; i < MaxFilters; i++){
		filters[i].allowedNotifications = 0;
	}
}

void ObserverRegistryManager::RemoveAllFilters()
{
	AutoLock locker(_mutex);
	for (short i = 0; i < MaxFilters; i++) {
		if (filters[i].allowedNotifications) {
			filters[i].allowedNotifications = 0;
			ExFreePool(filters[i].registryRootName.Buffer);
		}
	}
	filtersCount = 0;
}

NTSTATUS ObserverRegistryManager::AddFilter(const ClientRegistryFilter* filter)
{
	AutoLock locker(_mutex);
	
	if (filtersCount == MaxFilters)
		return STATUS_TOO_MANY_NAMES;
	auto rootKeyName = (PCWSTR)((PUCHAR)filter + sizeof(ClientRegistryFilter));

	int index = FindFilter(rootKeyName);

	if (index < 0) {
		UNICODE_STRING rootName;
		RtlInitUnicodeString(&rootName, rootKeyName);
		WCHAR* buffer = (WCHAR*)ExAllocatePool2(POOL_FLAG_PAGED, rootName.Length, DRIVER_TAG);
		if (!buffer) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		for (index = 0; index < MaxFilters; index++) {
			if (!filters[index].allowedNotifications) {
				filters[index].registryRootName.Buffer = buffer;
				filters[index].registryRootName.MaximumLength = rootName.Length;
				RtlCopyUnicodeString(&filters[index].registryRootName, &rootName);
				filters[index].allowedNotifications = filter->allowedOperations;
				filtersCount++;
				break;
			}
		}
	}
	filters[index].allowedNotifications = filter->allowedOperations;
	return STATUS_SUCCESS;
}

bool ObserverRegistryManager::RemoveFilter(PCWSTR rootKeyName)
{
	AutoLock locker(_mutex);
	int filterIndex = FindFilter(rootKeyName);
	if (filterIndex >= 0) {
		filters[filterIndex].allowedNotifications = 0;
		ExFreePool(filters[filterIndex].registryRootName.Buffer);
		filtersCount--;
		return true;
	} else {
		return false;
	}
}

int ObserverRegistryManager::FindFilter(PCWSTR rootKeyName)
{
	if(!filtersCount)
		return -1;
	UNICODE_STRING strRootKeyName;
	RtlInitUnicodeString(&strRootKeyName, rootKeyName);
	for (short i = 0; i < MaxFilters; i++) {
		if (filters[i].allowedNotifications &&
			RtlEqualUnicodeString(&strRootKeyName, &filters[i].registryRootName, TRUE)) {
			return i;
		}
	}
	return -1;
}

bool ObserverRegistryManager::Filter(const REG_NOTIFY_CLASS& notification, PCUNICODE_STRING keyName)
{
	AutoLock locker(_mutex);

	if (!filtersCount)
		return true;
	
	for (short i = 0; i < MaxFilters; i++) {
		if (filters[i].allowedNotifications && 
			keyName->Length >= filters[i].registryRootName.Length && 
			memcmp(filters[i].registryRootName.Buffer, keyName->Buffer, 
			filters[i].registryRootName.Length) == 0) {
			if (filters[i].allowedNotifications & RegistryOperationTypes::All) {
				return true;
			}
			if (filters[i].allowedNotifications & RegistryOperationTypes::None) {
				return false;
			}

			if (filters[i].allowedNotifications & RegistryOperationTypes::Create) {
				if (notification == RegNtPostCreateKeyEx) return true;
			}

			if (filters[i].allowedNotifications & RegistryOperationTypes::Delete) {
				if (notification == RegNtPostDeleteValueKey 
					|| notification == RegNtPostDeleteKey) return true;
			}

			if (filters[i].allowedNotifications & RegistryOperationTypes::Set) {
				if (notification == RegNtPostSetValueKey) return true;
			}
			return false;
		}
	}
	return false;
}