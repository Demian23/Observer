#pragma once

#include "List.h"
#include "ObserverRegistryManager.h"

struct ObserverContext final{
	List RegistryNotifications;
	ObserverRegistryManager RegistryManager;
	LARGE_INTEGER RegCookie;
	UNICODE_STRING RegistryRootPath;

	void Init();
	NTSTATUS SetRegistryRootPath(PCUNICODE_STRING path);
	NTSTATUS ReadSettingsFromRegistryAndApply();

private:
	bool TryOpenExistingKey(HANDLE root, PUNICODE_STRING subkeyName, PHANDLE result);
	NTSTATUS ReadKeyValue(HANDLE key, PUNICODE_STRING valueName,
		PKEY_VALUE_PARTIAL_INFORMATION* value);
	NTSTATUS SetFiltersFromRegistry(PKEY_VALUE_PARTIAL_INFORMATION names,
		PKEY_VALUE_PARTIAL_INFORMATION accesses);
};
