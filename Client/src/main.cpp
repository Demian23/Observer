#include "pch.h"
#include "ObserverClientInfo.h"
static int counter = 0;
int Error(const char* msg)
{
    printf("%s: error=%u\n", msg, ::GetLastError()); return 1;
}

void DisplayTime(const LARGE_INTEGER& time)
{
    SYSTEMTIME st;
	LARGE_INTEGER localTime{};
	FileTimeToLocalFileTime((const LPFILETIME)(&time), (LPFILETIME)&localTime);
    FileTimeToSystemTime((LPFILETIME)&localTime, &st);
    printf("%02d:%02d:%02d.%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayBinary(const UCHAR* buffer, DWORD size)
{
	for (DWORD i = 0; i < size; i++)
		printf("%02X ", buffer[i]);
	printf("\n");
}

void DisplayRegistryValue(const RegistrySetValue* info)
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
		DisplayTime(header->time);
		switch (header->type) {
		case ItemType::RegistrySetValue:
		{
			auto info = reinterpret_cast<RegistrySetValue*>(buffer);
			printf("Registry write PID=%u, TID=%u: %ws\\%ws type: %d size: %d data: ",
				info->processId, info->threadId,
				(PCWSTR)((PBYTE)info + info->keyNameOffset),
				(PCWSTR)((PBYTE)info + info->valueNameOffset),
				info->dataType, info->dataSize);
			DisplayRegistryValue(info);
			break;
		}
		case ItemType::RegistryDeleteValue:
		{
			auto info = reinterpret_cast<RegistryDeleteValue*>(buffer);
			printf("Registry delete value PID=%u, TID=%u: %ws %ws\n",
				info->processId, info->threadId,
				(PCWSTR)((PBYTE)info + info->keyNameOffset),
				(PCWSTR)((PBYTE)info + info->valueNameOffset));
			break;
		}
		case ItemType::RegistryCreateKey: 
		{
			auto info = reinterpret_cast<RegistryCreateKey*>(buffer);
			printf("Registry created key PID=%u, TID=%u: %ws (%ws)\n",
				info->processId, info->threadId,
				(PCWSTR)((PBYTE)info + info->keyNameOffset),
				info->relativeNameOffset ? (PCWSTR)((PBYTE)info + info->relativeNameOffset):L"");
			break;
		}
		case ItemType::RegistryDeleteKey: 
		{
			auto info = reinterpret_cast<RegistryDeleteKey*>(buffer);
			printf("Registry deleted key PID=%u, TID=%u: %ws\n",
				info->processId, info->threadId,
				(PCWSTR)((PBYTE)info + info->keyNameOffset));
			break;
		}
		default:
			printf("No such type: %d, bytes: %d\n", header->type, size);
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
	BYTE filterbuffer[256]{};

	ClientRegistryFilter* filter = (ClientRegistryFilter*)filterbuffer;
	filter->allowedOperations = (Set | Delete);
	constexpr WCHAR rootKey[18] = L"\\REGISTRY\\MACHINE";
	filter->rootKeyNameSizeInBytes = 18 * sizeof(WCHAR);
	memcpy((PUCHAR)filter + sizeof(ClientRegistryFilter), rootKey,
		18 * sizeof(WCHAR));
	if(!DeviceIoControl(hDevice, IOCTL_OBSERVER_ADD_FILTER, filter, 
		filter->rootKeyNameSizeInBytes + sizeof(ClientRegistryFilter), NULL, 0, NULL, NULL))
		return Error("Failed to set filter");
	bool reset = false;
	while (true) {
		DWORD bytes;
		if (!::ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr))
			return Error("Failed to read");
		if (bytes != 0)
			DisplayInfo(buffer, bytes);
		else {
			Sleep(1000);
			counter++;
		}
		
		::Sleep(200);
		if(counter == 10){
			if (!DeviceIoControl(hDevice, IOCTL_OBSERVER_REMOVE_ALL_FILTERS, NULL,
				NULL, NULL, 0, NULL, NULL))
				return Error("Failed to remove all filters");
			else
				printf("REMOVE ALL FILTERS\n");
			Sleep(1000);
			reset = true;
		} else
			if (reset) {
				if(!DeviceIoControl(hDevice, IOCTL_OBSERVER_ADD_FILTER, filter, 
					filter->rootKeyNameSizeInBytes + sizeof(ClientRegistryFilter), NULL, 0, NULL, NULL))
					return Error("Failed to set second filter");
				else
					printf("SET FILTER in second time\n");
				counter++;
				reset = false;
			}else
				if (counter == 30) {
					if(!DeviceIoControl(hDevice, IOCTL_OBSERVER_REMOVE_FILTER, (PVOID)rootKey, 
						18 * sizeof(WCHAR), NULL, 0, NULL, NULL))
					return Error("Failed to remove second filter");
				else
					printf("Remove FILTER in second time\n");
				}
	}
    CloseHandle(hDevice);
}