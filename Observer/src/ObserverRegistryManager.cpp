#include "pch.h"
#include "AutoLock.h"
#include "ObserverRegistryManager.h"

void ObserverRegistryManager::Init()
{
	_mutex.Init();
	_fsMutex.Init();
	for(short i = 0; i < MaxFilters; i++){
		filters[i].allowedNotifications = 0;
	}
	for(short i = 0; i < MaxFilters; i++){
		fsFilters[i].fileNameRoot.Length = fsFilters[i].fileNameRoot.MaximumLength = 0;
	}
	fsFiltersCount = filtersCount = 0;
}

bool ObserverRegistryManager::AddFSFilterFromKernel(const FilesystemFilter filter)
{
	AutoLock locker(_fsMutex);
	if (fsFiltersCount >= MaxFilters)
		return false;

	short i;
	for (i = 0; i < MaxFilters; i++)
		if (!fsFilters[i].fileNameRoot.Length)
			break;
	if (i != MaxFilters) {
		fsFilters[i] = filter;
		fsFiltersCount++;
		return true;
	} else {
		return false;
	}
}

bool ObserverRegistryManager::AddFilterFromKernel(const RegistryFilter filter)
{
	AutoLock locker(_mutex);

	int index = FindExistingFilterIndex(&filter.registryRootName);
	if (index != -1) {
		RemoveFilter(index);
	} else {
		index = FindFreeIndex();
		if (index == -1)
			return false;
	}
	filtersCount++;
	filters[index] = filter;
	return true;
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
		index = FindFreeIndex();
		filters[index].registryRootName.Buffer = buffer;
		filters[index].registryRootName.MaximumLength = rootName.Length;
		RtlCopyUnicodeString(&filters[index].registryRootName, &rootName);
		filters[index].allowedNotifications = filter->allowedOperations;
		filtersCount++;
	}
	filters[index].allowedNotifications = filter->allowedOperations;
	return STATUS_SUCCESS;
}

bool ObserverRegistryManager::RemoveFilter(PCWSTR rootKeyName)
{
	AutoLock locker(_mutex);
	int filterIndex = FindFilter(rootKeyName);
	return RemoveFilter(filterIndex);
}

int ObserverRegistryManager::FindFilter(PCWSTR rootKeyName)
{
	if(!filtersCount)
		return -1;
	UNICODE_STRING strRootKeyName;
	RtlInitUnicodeString(&strRootKeyName, rootKeyName);
	return FindExistingFilterIndex(&strRootKeyName);
}

bool ObserverRegistryManager::FilterFS(PCUNICODE_STRING fileName)
{
	AutoLock locker(_fsMutex);

	if (!fsFiltersCount)
		return true;
	for(short i = 0; i < MaxFilters; i++)
		if (fsFilters[i].fileNameRoot.Length &&
			fileName->Length >= filters[i].registryRootName.Length &&
			memcmp(fsFilters[i].fileNameRoot.Buffer, fileName->Buffer,
				fsFilters[i].fileNameRoot.Length) == 0) 
		{
			return false;
		}
	return true;
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

bool ObserverRegistryManager::RemoveFilter(int filterIndex)
{
	if (filterIndex >= 0) {
		filters[filterIndex].allowedNotifications = 0;
		ExFreePool(filters[filterIndex].registryRootName.Buffer);
		filtersCount--;
		return true;
	} else {
		return false;
	}
}

void ObserverRegistryManager::Dispose()
{
	// lock?
	for (short i = 0; i < MaxFilters; i++) {
		if (filters[i].allowedNotifications)
			ExFreePool(filters[i].registryRootName.Buffer);
	}
	for (short i = 0; i < MaxFilters; i++) {
		if (fsFilters[i].fileNameRoot.Length)
			ExFreePool(fsFilters[i].fileNameRoot.Buffer);
	}
}