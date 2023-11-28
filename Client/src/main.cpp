#include "pch.h"
#include "ObserverPublic.h"

int Error(const char* msg)
{
    printf("%s: error=%u\n", msg, ::GetLastError()); return 1;
}
void DisplayTime(const LARGE_INTEGER& time)
{
    SYSTEMTIME st;
    ::FileTimeToSystemTime((FILETIME*)&time, &st);
    printf("%02d:%02d:%02d.%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayBinary(const UCHAR* buffer, DWORD size)
{
	for (DWORD i = 0; i < size; i++)
		printf("%02X ", buffer[i]);
	printf("\n");
}

void DisplayRegistryValue(const RegistrySet* info)
{
	auto data = (PBYTE)info + info->dataOffset;
	switch (info->dataType) {
	case REG_DWORD:
		printf("0x%08X (%u)\n", *(DWORD*)data, *(DWORD*)data); break;
	case REG_SZ:
	case REG_EXPAND_SZ:
		printf("%ws\n", (PCWSTR)data); break;
		// add other cases... (REG_QWORD, REG_LINK, etc.)
	default:
		DisplayBinary(data, info->providedDataSize); break;
	}
}


void DisplayInfo(BYTE* buffer, DWORD size)
{
	auto count = size;
	while (count > 0) {
		auto header = (ItemHeader*)buffer;
		switch (header->type) {
		case ItemType::RegistrySetValue:
		{
			DisplayTime(header->time);
			auto info = (RegistrySet*)buffer;
			printf("Registry write PID=%u, TID=%u: %ws\\%ws type: %d size: %d data: ",
				info->processId, info->threadId,
				(PCWSTR)((PBYTE)info + info->keyNameOffset),
				(PCWSTR)((PBYTE)info + info->valueNameOffset),
				info->dataType, info->dataSize);
			DisplayRegistryValue(info);
			break;
		}
		default:
			break;
		}
		buffer += header->size;
		count -= header->size;
	}

}

int main()
{
    HANDLE hDevice = CreateFile(L"\\\\.\\Observer", GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr); 
    if (hDevice == INVALID_HANDLE_VALUE) {
        return Error("Failed to open device");
    }

	BYTE buffer[1 << 16]{};

	while (true) {
		DWORD bytes;
		if (!::ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr))
			return Error("Failed to read");

		if (bytes != 0)
			DisplayInfo(buffer, bytes);

		::Sleep(200);
	}
    CloseHandle(hDevice);
}