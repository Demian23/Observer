#pragma once

struct File {
	ULONG key;
	volatile LONG64 writesAmount;
	USHORT fileNameSize;
	USHORT fileNameOffset;
};

inline RTL_GENERIC_COMPARE_RESULTS TableFileCompare(PRTL_GENERIC_TABLE, PVOID first, PVOID second)
{
	auto p1 = (File*)first;
	auto p2 = (File*)second;

	if (p1->key == p2->key)
		return GenericEqual;

	return p1->key > p2->key ? GenericGreaterThan : GenericLessThan;
}
