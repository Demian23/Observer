#include "pch.h"
#include "Observer.h"
#include "ObserverContext.h"

NTSTATUS ObserverContext::ReadSettingsFromRegistryAndInit(PUNICODE_STRING RegistryPath)
{
	RegistryManager.Init();
	NTSTATUS status;
	OBJECT_ATTRIBUTES keyAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(RegistryPath, OBJ_KERNEL_HANDLE);
	HANDLE hRootKey = nullptr, hSettingsKey = nullptr, hFiltersKey = nullptr;
	PKEY_VALUE_PARTIAL_INFORMATION filtersNames = nullptr, desiredNotifications = nullptr,
		notificationsMaxStorageSize = nullptr;
	enum class ResourceAllocationsStages {None, OpenedRootKey, OpenedSettingsKey,
		NotificatinStorageMaxSizeAllocated, OpenedFiltersKey, 
		FiltersNamesAllocated, DesiredNotificationsAllocated} 
		resourceStage = ResourceAllocationsStages::None;
	do{
		status = ZwOpenKey(&hRootKey, KEY_WRITE, &keyAttr);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX"Failed open root registry path (0x%08X)\n", status));
			break;
		}
		resourceStage = ResourceAllocationsStages::OpenedRootKey;

		UNICODE_STRING strSettingsKey = RTL_CONSTANT_STRING(L"Settings");
		if (!TryOpenExistingKey(hRootKey, &strSettingsKey, &hSettingsKey)) {
			KdPrint((DRIVER_PREFIX"Failed open existed settings registry path\n"));
			break;
		} 
		resourceStage = ResourceAllocationsStages::OpenedSettingsKey;

		UNICODE_STRING valueNotificationsMaxStorageSize = RTL_CONSTANT_STRING(L"MaxNotifications");
		if (!NT_SUCCESS(ReadKeyValue(hSettingsKey, 
			&valueNotificationsMaxStorageSize , &notificationsMaxStorageSize))) {
			break;
		}
		// TODO check value type
		if (notificationsMaxStorageSize == nullptr) {
			break;
		}
		InitNotificationStorage(notificationsMaxStorageSize);


		UNICODE_STRING strFiltersKey = RTL_CONSTANT_STRING(L"RegistryFilters");
		if (!TryOpenExistingKey(hSettingsKey, &strFiltersKey, &hFiltersKey)) {
			KdPrint((DRIVER_PREFIX"Failed open existed filters registry path\n"));
			break;
		} 
		resourceStage = ResourceAllocationsStages::OpenedFiltersKey;

		// Read registry filters settings

		UNICODE_STRING valueFilteredRootKeys = RTL_CONSTANT_STRING(L"FilteredKeys");
		if (!NT_SUCCESS(ReadKeyValue(hFiltersKey, &valueFilteredRootKeys, &filtersNames))) {
			break;
		}
		// TODO check value type
		if (filtersNames == nullptr) {
			break;
		}
		resourceStage = ResourceAllocationsStages::FiltersNamesAllocated;

		UNICODE_STRING valueDesiredNotifications = RTL_CONSTANT_STRING(L"DesiredNotifications");
		if (!NT_SUCCESS(ReadKeyValue(hFiltersKey, &valueDesiredNotifications, &desiredNotifications))) {
			break;
		}
		if (desiredNotifications == nullptr) {
			break;
		}
		resourceStage = ResourceAllocationsStages::DesiredNotificationsAllocated;

		status = SetFiltersFromRegistry(filtersNames, desiredNotifications);
	} while (false);
	
	switch (resourceStage) {
	case ResourceAllocationsStages::DesiredNotificationsAllocated:
		ExFreePool(desiredNotifications);
		[[fallthrough]];
	case ResourceAllocationsStages::FiltersNamesAllocated:
		ExFreePool(filtersNames);
		[[fallthrough]];
	case ResourceAllocationsStages::OpenedFiltersKey:
		ZwClose(hFiltersKey);
		[[fallthrough]];
	case ResourceAllocationsStages::NotificatinStorageMaxSizeAllocated:
		ExFreePool(notificationsMaxStorageSize);
		[[fallthrough]];
	case ResourceAllocationsStages::OpenedSettingsKey:
		ZwClose(hSettingsKey);
		[[fallthrough]];
	case ResourceAllocationsStages::OpenedRootKey:
		ZwClose(hRootKey);
	}
	return status;
}

void ObserverContext::InitNotificationStorage(PKEY_VALUE_PARTIAL_INFORMATION maxStorageSize)
{ 
	ULONG size = 0xFFFF;
	if (maxStorageSize->Type == REG_DWORD) {
		size = *(ULONG*)maxStorageSize->Data;
	} else {
		KdPrint((DRIVER_PREFIX"Wrong registry parameter (max storage size) type\n"));
	}
	RegistryNotifications.Init(size);
}

bool ObserverContext::TryOpenExistingKey(HANDLE root, PUNICODE_STRING subkeyName, 
	PHANDLE result)
{
	OBJECT_ATTRIBUTES subKeyAttr{};
	InitializeObjectAttributes(&subKeyAttr, subkeyName, OBJ_KERNEL_HANDLE, root, nullptr);
	ULONG subKeyDisposition;
	auto status = ZwCreateKey(result, KEY_QUERY_VALUE, &subKeyAttr, 0, nullptr, 0, &subKeyDisposition);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX"Failed open subkey registry path (0x%08X)\n", status));
		return false;
	}
	// new key is empty
	if (subKeyDisposition == REG_CREATED_NEW_KEY) {
		ZwClose(*result);
		return false;
	} else
		return true;
}

 NTSTATUS ObserverContext::ReadKeyValue(HANDLE key, PUNICODE_STRING valueName, 
	 PKEY_VALUE_PARTIAL_INFORMATION* value)
{
	NTSTATUS status;
	PKEY_VALUE_PARTIAL_INFORMATION keyInfo = nullptr;
	ULONG neccessaryLength = 0, actualLength = 0;
	bool isKeyInfoAllocated = false;
	do {
		status = ZwQueryValueKey(key, valueName,
			KeyValuePartialInformation, keyInfo, 0, &neccessaryLength);
		// empty buffer -> success
		if (NT_SUCCESS(status)) {
			break;
		}
		if (status != STATUS_BUFFER_TOO_SMALL && status != STATUS_BUFFER_OVERFLOW) {
			KdPrint((DRIVER_PREFIX"Error query value from registry settings (0x%08X)\n", status));
			break;
		}
		// TODO should think, where allocate 
		keyInfo = (PKEY_VALUE_PARTIAL_INFORMATION)
			ExAllocatePool2(POOL_FLAG_PAGED, neccessaryLength, DRIVER_TAG);
		if (keyInfo == nullptr) {
			KdPrint((DRIVER_PREFIX"Failed to allocate for keyInfo\n"));
			status = STATUS_NO_MEMORY;
			break;
		}
		isKeyInfoAllocated = true;

		status = ZwQueryValueKey(key, valueName, KeyValuePartialInformation,
			keyInfo, neccessaryLength, &actualLength);
		if (!NT_SUCCESS(status) || actualLength != neccessaryLength) {
			KdPrint((DRIVER_PREFIX"Failed to query from registry settings (0x%08X)\n", status));
			break;
		} else {
			*value = keyInfo;
		}
	} while (false);

	if (!NT_SUCCESS(status) && isKeyInfoAllocated)
		ExFreePool(keyInfo);
	return status;
}

 NTSTATUS ObserverContext::SetFiltersFromRegistry(PKEY_VALUE_PARTIAL_INFORMATION names,
	 PKEY_VALUE_PARTIAL_INFORMATION accesses)
 {
	 NTSTATUS status = STATUS_SUCCESS;
	 if (names->Type == REG_SZ && accesses->Type == REG_BINARY) {
		 constexpr WCHAR keyNamesDelimeter = L';';
		 ULONG amountOfFiltersNames = 0;
		 PCWSTR strNames = (PCWSTR)names->Data, tempPtr;
		 PUCHAR bytesAccesses = accesses->Data;
		 while ((tempPtr = wcschr(strNames, keyNamesDelimeter)) != nullptr) {
			 strNames = tempPtr + 1;
			 amountOfFiltersNames++;
		 }
		 strNames = (PCWSTR)names->Data;
		 // amount of accesses equals amount of keys
		 if (accesses->DataLength == amountOfFiltersNames) {
			 for (USHORT offset = 0, newKeyNameFirstSymbol = 0; 
				 strNames[offset]; offset++) {
				 if (strNames[offset] == keyNamesDelimeter){ 
					 USHORT nameLenInBytes = (offset - newKeyNameFirstSymbol) * sizeof(WCHAR);
					 PWCHAR name = (PWCHAR)ExAllocatePool2(POOL_FLAG_PAGED, 
						 nameLenInBytes, DRIVER_TAG);
					 if (name == nullptr) {
						 KdPrint((DRIVER_PREFIX"Failed allocate name (0x%08X)\n", status));
						 status = STATUS_NO_MEMORY;
						 break;
					 }

					 ObserverRegistryManager::RegistryFilter filter{};
					 filter.allowedNotifications = *bytesAccesses++;
					 filter.registryRootName.Buffer = name;
					 filter.registryRootName.MaximumLength = 
						 filter.registryRootName.Length = nameLenInBytes;
					 memcpy((PUCHAR)name, (PUCHAR)(strNames + newKeyNameFirstSymbol), 
						 nameLenInBytes);

					 RegistryManager.AddFilterFromKernel(filter);
					 newKeyNameFirstSymbol = offset + 1;
				 }
			 }
		 } else {
			KdPrint((DRIVER_PREFIX"Wrong types for registry settings\n"));
			status = STATUS_INVALID_BUFFER_SIZE; // don't know what status return
		 }
	 } else {
		KdPrint((DRIVER_PREFIX"Wrong types for registry settings\n"));
		status = STATUS_INVALID_PARAMETER; // don't know what status return
	 }
	 return status;
 }
