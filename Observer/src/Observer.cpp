#include "pch.h"
#include "Observer.h"
#include "ObserverContext.h"
#include "../../Client/src/ObserverClientInfo.h"

NTSTATUS ObserverCreateClose(PDEVICE_OBJECT, PIRP Irp);
void ObserverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS ObserverRead(PDEVICE_OBJECT, PIRP Irp);
NTSTATUS ObserverDeviceControl(PDEVICE_OBJECT, PIRP Irp);

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR);
NTSTATUS OnRegistryNotify(PVOID context, PVOID notifyClass, PVOID arg);

static ObserverContext observerContext;

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	UNICODE_STRING devName = RTL_CONSTANT_STRING(ObserverDeviceName);
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(ObserverDeviceSymlink);

	PDEVICE_OBJECT DeviceObject = nullptr;
	NTSTATUS status = STATUS_SUCCESS;

	enum class InitStage{None, DeviceCreated, LinkCreated, CallbackRegistered} initStage = InitStage::None;
	
	do {
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, 
			&DeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n", status));
			break;
		}

		DeviceObject->Flags |= DO_DIRECT_IO;
		initStage = InitStage::DeviceCreated;
		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create symbolic link (0x%08X)\n", status));
			break;
		}
		initStage = InitStage::LinkCreated;
		UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"7657.123");
		status = CmRegisterCallbackEx(OnRegistryNotify, &altitude, DriverObject, nullptr, &observerContext.RegCookie, nullptr);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to set registry callback (0x%08X)\n", status));
			break;
		}
		initStage = InitStage::CallbackRegistered;
	} while (false);

	if (!NT_SUCCESS(status)) {
		switch (initStage) {
		case InitStage::CallbackRegistered:
			CmUnRegisterCallback(observerContext.RegCookie);
			[[fallthrough]];
		case InitStage::LinkCreated:
			IoDeleteSymbolicLink(&symLink);
			[[fallthrough]];
		case InitStage::DeviceCreated:
			IoDeleteDevice(DeviceObject);
			[[fallthrough]];
		default: break;
		}
		return status;
	}

	DriverObject->DriverUnload = ObserverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] =
		ObserverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ObserverRead;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ObserverDeviceControl;

	observerContext.RegistryNotifications.Init(10000);
	observerContext.RegistryManager.Init();

	return status;
}

NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS ObserverCreateClose(PDEVICE_OBJECT, PIRP Irp)
{
	return CompleteIrp(Irp);
}

void ObserverUnload(PDRIVER_OBJECT DriverObject)
{
	LIST_ENTRY* entry;
	while ((entry = observerContext.RegistryNotifications.RemoveItem()) != nullptr)
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Observer");
	CmUnRegisterCallback(observerContext.RegCookie);
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS ObserverRead(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG bytes = 0;
	if (len == 0)
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);

	NT_ASSERT(Irp->MdlAddress);

	auto buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
		status = STATUS_INSUFFICIENT_RESOURCES;
	else {
		while (true) {
			auto entry = observerContext.RegistryNotifications.RemoveItem();
			if (entry != nullptr) {
				auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
				auto size = info->Data.size;
				if (len >= size) {
					memcpy(buffer, &info->Data, size);
					len -= size;
					buffer += size;
					bytes += size;
					ExFreePool(info); 
				} else {
					observerContext.RegistryNotifications.RemoveItem();
					break;
				}
			} else {
				break;
			}
		}
	}
	return CompleteIrp(Irp, status, bytes);
}

void OnRegistrySetValue(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName);
void OnRegistryCreateKey(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName);
void OnRegistryDeleteKey(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName);
void OnRegistryDeleteValue(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName);

NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2)
{
	UNREFERENCED_PARAMETER(context);

	REG_NOTIFY_CLASS notifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)arg1;
	// check only RegNtPostCreateKeyEx, RegNtPostSetValueKey, RegNtPostDeleteKey, RegNtPostDeleteValueKey
	// RegNtPostRenameKey -> need to listen pre notify, get old name, write to context, than post 
	// for all them arg2 is RegPostOperationInformation

	if (notifyClass == RegNtPostCreateKeyEx || notifyClass == RegNtPostSetValueKey ||
		notifyClass == RegNtPostDeleteKey || notifyClass == RegNtPostDeleteValueKey){

		auto notificationInfo = (REG_POST_OPERATION_INFORMATION*)arg2;
		if (NT_SUCCESS(notificationInfo->Status)) {
			PCUNICODE_STRING keyName;
			if (NT_SUCCESS(CmCallbackGetKeyObjectIDEx(&observerContext.RegCookie,
				notificationInfo->Object, nullptr, &keyName, 0)) && 
				observerContext.RegistryManager.Filter(notifyClass, keyName)) {

				switch (notifyClass) {
				case RegNtPostSetValueKey:
					OnRegistrySetValue(notificationInfo, keyName);
					break;
				case RegNtPostCreateKeyEx:
					OnRegistryCreateKey(notificationInfo, keyName);
					break;
				case RegNtPostDeleteKey:
					// maybe I can't get keyName from Object here, in this case i should look into preInfo
					OnRegistryDeleteKey(notificationInfo, keyName);
					break;
				case RegNtPostDeleteValueKey:
					OnRegistryDeleteValue(notificationInfo, keyName);
					break;
				}
				CmCallbackReleaseKeyObjectIDEx(keyName);
			}
		}
	}
	return STATUS_SUCCESS;
}

void OnRegistrySetValue(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName)
{
	auto preInfo = (REG_SET_VALUE_KEY_INFORMATION*)notificationInfo->PreInformation;
	NT_ASSERT(preInfo);

	USHORT size = sizeof(RegistrySetValue);
	USHORT keyNameLen = keyName->Length + sizeof(WCHAR);
	USHORT valueNameLen = preInfo->ValueName->Length + sizeof(WCHAR);
	USHORT valueSize = static_cast<USHORT>(min(256, preInfo->DataSize));

	size += keyNameLen + valueNameLen + valueSize;
	auto info = (FullItem<RegistrySetValue>*)(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
	if (info) {
		auto& data = info->Data;
		KeQuerySystemTimePrecise(&data.time);
		data.type = ItemType::RegistrySetValue;
		data.size = size;
		data.dataType = preInfo->Type;
		data.processId = HandleToULong(PsGetCurrentProcessId());
		data.threadId = HandleToULong(PsGetCurrentThreadId());
		data.providedDataSize = valueSize;
		data.dataSize = preInfo->DataSize;
		USHORT offset = sizeof(data);
		data.keyNameOffset = offset;
		wcsncpy_s((PWSTR)((PUCHAR)&data + offset), keyNameLen / sizeof(WCHAR), 
			keyName->Buffer, keyName->Length / sizeof(WCHAR));
		offset += keyNameLen;
		data.valueNameOffset = offset;
		wcsncpy_s((PWSTR)((PUCHAR)&data + offset), valueNameLen / sizeof(WCHAR), 
			preInfo->ValueName->Buffer, preInfo->ValueName->Length / sizeof(WCHAR));
		offset += valueNameLen;
		data.dataOffset = offset;
		memcpy((PUCHAR)&data + offset, preInfo->Data, valueSize);
		observerContext.RegistryNotifications.AddItem(&info->Entry);
	} else {
		KdPrint((DRIVER_PREFIX"Failed to allocate memeory for registry set value\n"));
	}
}

void OnRegistryCreateKey(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName)
{
	// can only compare key name in preinfo and result keyName
	// key can already exist!
	// relative offset is useless
	auto preInfo = (REG_CREATE_KEY_INFORMATION*)notificationInfo->PreInformation;
	NT_ASSERT(preInfo);
	if (*(preInfo->Disposition) == REG_CREATED_NEW_KEY) {
		USHORT keyNameLen = keyName->Length + sizeof(WCHAR);
		USHORT relativeNameLen = preInfo->CompleteName->Length + sizeof(WCHAR);
		USHORT size = sizeof(RegistryCreateKey) + keyNameLen + relativeNameLen;
		auto info = (FullItem<RegistryCreateKey>*)(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
		if (info) {
			auto& data = info->Data;
			KeQuerySystemTimePrecise(&data.time);
			data.type = ItemType::RegistryCreateKey;
			data.size = size;
			data.processId = HandleToULong(PsGetCurrentProcessId());
			data.threadId = HandleToULong(PsGetCurrentThreadId());
			USHORT offset = sizeof(data);
			data.keyNameOffset = offset;
			wcsncpy_s((PWSTR)((PUCHAR)&data + offset), keyNameLen / sizeof(WCHAR), 
				keyName->Buffer, keyName->Length / sizeof(WCHAR));
			if (relativeNameLen > 1) {
				offset += keyNameLen;
				data.relativeNameOffset = offset;
				wcsncpy_s((PWSTR)((PUCHAR)&data + offset), relativeNameLen / sizeof(WCHAR), 
				preInfo->CompleteName->Buffer, preInfo->CompleteName->Length / sizeof(WCHAR));
			} else {
				data.relativeNameOffset = 0;
			}
			observerContext.RegistryNotifications.AddItem(&info->Entry);
		} else {
			KdPrint((DRIVER_PREFIX"Failed to allocate memeory for registry create value\n"));
		}
	}
}

void OnRegistryDeleteKey(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName)
{
	UNREFERENCED_PARAMETER(notificationInfo);
	USHORT keyNameLen = keyName->Length + sizeof(WCHAR);
	USHORT size = sizeof(RegistryCreateKey) + keyNameLen;
	auto info = (FullItem<RegistryCreateKey>*)(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
	if (info) {
		auto& data = info->Data;
		KeQuerySystemTimePrecise(&data.time);
		data.type = ItemType::RegistryDeleteKey;
		data.size = size;
		data.processId = HandleToULong(PsGetCurrentProcessId());
		data.threadId = HandleToULong(PsGetCurrentThreadId());
		USHORT offset = sizeof(data);
		data.keyNameOffset = offset;
		wcsncpy_s((PWSTR)((PUCHAR)&data + offset), keyNameLen / sizeof(WCHAR), 
			keyName->Buffer, keyName->Length / sizeof(WCHAR));
		observerContext.RegistryNotifications.AddItem(&info->Entry);
	} else {
		KdPrint((DRIVER_PREFIX"Failed to allocate memeory for registry create value\n"));
	}

}

void OnRegistryDeleteValue(REG_POST_OPERATION_INFORMATION* notificationInfo, PCUNICODE_STRING keyName)
{
	auto preInfo = (PREG_DELETE_VALUE_KEY_INFORMATION)notificationInfo->PreInformation;
	NT_ASSERT(preInfo);

	USHORT size = sizeof(RegistryDeleteValue);
	USHORT keyNameLen = keyName->Length + sizeof(WCHAR);
	USHORT valueNameLen = preInfo->ValueName->Length + sizeof(WCHAR);

	size += keyNameLen + valueNameLen;
	auto info = (FullItem<RegistryDeleteValue>*)(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
	if (info) {
		auto& data = info->Data;
		KeQuerySystemTimePrecise(&data.time);
		data.type = ItemType::RegistryDeleteValue;
		data.size = size;
		data.processId = HandleToULong(PsGetCurrentProcessId());
		data.threadId = HandleToULong(PsGetCurrentThreadId());
		USHORT offset = sizeof(data);
		data.keyNameOffset = offset;
		wcsncpy_s((PWSTR)((PUCHAR)&data + offset), keyNameLen / sizeof(WCHAR), 
			keyName->Buffer, keyName->Length / sizeof(WCHAR));
		offset += keyNameLen;
		data.valueNameOffset = offset;
		wcsncpy_s((PWSTR)((PUCHAR)&data + offset), valueNameLen / sizeof(WCHAR), 
			preInfo->ValueName->Buffer, preInfo->ValueName->Length / sizeof(WCHAR));
		observerContext.RegistryNotifications.AddItem(&info->Entry);
	} else {
		KdPrint((DRIVER_PREFIX"Failed to allocate memeory for registry set value\n"));
	}
}

NTSTATUS ObserverDeviceControl(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_INVALID_DEVICE_REQUEST;
	auto code = stack->Parameters.DeviceIoControl.IoControlCode;

	switch (code) {
	case IOCTL_OBSERVER_ADD_FILTER: {
		auto clientFilter = (ClientRegistryFilter*)Irp->AssociatedIrp.SystemBuffer;
		auto len = stack->Parameters.DeviceIoControl.InputBufferLength;
		auto clientFilterSize = sizeof(ClientRegistryFilter) + clientFilter->rootKeyNameSizeInBytes;
		if (clientFilter == nullptr || len != clientFilterSize) {
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		status = observerContext.RegistryManager.AddFilter(clientFilter);
		break;
	}
	case IOCTL_OBSERVER_REMOVE_FILTER: {
		auto filterName = (PCWSTR)Irp->AssociatedIrp.SystemBuffer;
		auto len = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (filterName == nullptr || len == 0) {
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}
		auto removed = observerContext.RegistryManager.RemoveFilter(filterName);
		status = removed ? STATUS_SUCCESS : STATUS_NOT_FOUND;
		break;
	}
	case IOCTL_OBSERVER_REMOVE_ALL_FILTERS:
		observerContext.RegistryManager.RemoveAllFilters();
		status = STATUS_SUCCESS;
		break;
	}
	return CompleteIrp(Irp, status);
}
