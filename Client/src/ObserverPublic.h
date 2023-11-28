#pragma once

enum class ItemType : short {
	None,
	RegistrySetValue
};

struct ItemHeader {
	ItemType type;
	USHORT size;
	LARGE_INTEGER time;
};

struct Registry : ItemHeader {
	ULONG processId;
	ULONG threadId;
	USHORT keyNameOffset;
};

struct RegistrySet : Registry{
	USHORT valueNameOffset;
	ULONG dataType;
	ULONG dataSize;
	USHORT dataOffset;
	USHORT providedDataSize;
};
