#pragma once

// buffer field set in null
#define IOCTL_OBSERVER_REMOVE_ALL_FILTERS CTL_CODE(0x8000, 0x800, METHOD_NEITHER, FILE_ANY_ACCESS)
// pass ClientRegistryFilter structure through buffer
#define IOCTL_OBSERVER_ADD_FILTER CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
// pass reg key name (PCWSTR) through buffer
#define IOCTL_OBSERVER_REMOVE_FILTER CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_OBSERVER_UPDATE_FROM_REGISTRY CTL_CODE(0x8000, 0x803, METHOD_NEITHER, FILE_ANY_ACCESS)

enum RegistryOperationTypes{Create = 1, Set = 2, Delete = 4, All = 8, None = 16};

// keyName should be stored on offset sizeof(ClientRegistryFilter)
struct ClientRegistryFilter {
	USHORT allowedOperations;
	// optional
	USHORT rootKeyNameSizeInBytes;
};

enum class ItemType : short {
	None,
	RegistrySetValue,
	RegistryCreateKey,
	RegistryDeleteKey, 
	RegistryDeleteValue,
	FileSystemOpenFile,
	FileSystemWriteFile,
	FileSystemCleanupFile
};

struct ItemHeader {
	ItemType type;
	USHORT size;
	LARGE_INTEGER time;
};

struct RegistryKey : ItemHeader {
	ULONG processId;
	ULONG threadId;
	USHORT keyNameOffset;
};

struct RegistryValue : RegistryKey {
	USHORT valueNameOffset;
};

struct RegistrySetValue : RegistryValue{
	ULONG dataType;
	ULONG dataSize;
	USHORT dataOffset;
	USHORT providedDataSize;
};

struct RegistryCreateKey : RegistryKey {
	USHORT relativeNameOffset;
};

using RegistryDeleteValue = RegistryValue;
using RegistryDeleteKey = RegistryKey;

struct FileEvent : ItemHeader{
	ULONG processId;
	ULONG threadId;
	USHORT fileNameOffset;
};

using FileOpen = FileEvent;

struct FileWrite : FileEvent {
	ULONG writeOperationCount;
	ULONG bytesWritten;
};

struct FileCleanup : FileEvent {
	ULONG writesAmount;
};
