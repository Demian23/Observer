#pragma once

#include "List.h"
#include "ObserverRegistryManager.h"

struct ObserverContext final{
	List RegistryNotifications;
	ObserverRegistryManager RegistryManager;
	LARGE_INTEGER RegCookie;

	NTSTATUS ReadSettingsFromRegistryAndInit(PUNICODE_STRING RegistryPath);
private:
	bool TryOpenExistingKey(HANDLE root, PUNICODE_STRING subkeyName, PHANDLE result);
	void InitNotificationStorage(PKEY_VALUE_PARTIAL_INFORMATION maxStorageSize);
	NTSTATUS ReadKeyValue(HANDLE key, PUNICODE_STRING valueName,
		PKEY_VALUE_PARTIAL_INFORMATION* value);
	NTSTATUS SetFiltersFromRegistry(PKEY_VALUE_PARTIAL_INFORMATION names,
		PKEY_VALUE_PARTIAL_INFORMATION accesses);
};
