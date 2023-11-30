#pragma once

enum class ItemType : short {
	None,
	RegistrySetValue,
	RegistryCreateKey,
	RegistryDeleteKey, 
	RegistryDeleteValue
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

using RegistryDeleteValue = RegistryValue;
using RegistryCreateKey = RegistryKey;
using RegistryDeleteKey = RegistryKey;
