#pragma once

#include "List.h"
#include "Table.h"
#include "File.h"
#include "ObserverRegistryManager.h"

struct ObserverContext final{
	ObserverRegistryManager RegistryManager;
	UNICODE_STRING RegistryRootPath;
	LARGE_INTEGER RegCookie;
	PDEVICE_OBJECT DeviceObject;
	List ObservedEvents;
	Table<File, ULONG, TableFileCompare> processTable;

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
