#include "pch.h"
#include "Observer.h"
#include "Globals.h"
#include "../../Client/src/ObserverPublic.h"

NTSTATUS ObserverCreateClose(PDEVICE_OBJECT, PIRP Irp);
void ObserverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS ObserverRead(PDEVICE_OBJECT, PIRP Irp);
NTSTATUS ObserverWrite(PDEVICE_OBJECT, PIRP Irp);
NTSTATUS CompleteIrp(PIRP Irp, NTSTATUS status, ULONG_PTR);
NTSTATUS OnRegistryNotify(PVOID context, PVOID notifyClass, PVOID arg);

Globals g_Globals;

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = ObserverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] =
		ObserverCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = ObserverRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ObserverWrite;

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Observer");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Observer");

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
		status = CmRegisterCallbackEx(OnRegistryNotify, &altitude, DriverObject, nullptr, &g_Globals.RegCookie, nullptr);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to set registry callback (0x%08X)\n", status));
			break;
		}
		initStage = InitStage::CallbackRegistered;
	} while (false);
	if (!NT_SUCCESS(status)) {
		switch (initStage) {
		case InitStage::CallbackRegistered:
			CmUnRegisterCallback(g_Globals.RegCookie);
		case InitStage::LinkCreated:
			IoDeleteSymbolicLink(&symLink);
		case InitStage::DeviceCreated:
			IoDeleteDevice(DeviceObject);
		}
		return status;
	}
	g_Globals.list.init(10000); // TODO read this value from registry
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
	while ((entry = g_Globals.list.removeItem()) != nullptr)
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Observer");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
	CmUnRegisterCallback(g_Globals.RegCookie);
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
			auto entry = g_Globals.list.removeItem();
			if (entry != nullptr) {
				auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
				auto size = info->Data.size;
				if (len >= size) {
					memcpy(buffer, &info->Data, size);
					len -= size;
					buffer += size;
					bytes += size;
					ExFreePool(info); // why not exfreepollwithtag?
				} else {
					g_Globals.list.addItem(entry);
					break;
				}
			} else {
				break;
			}
		}
	}
	return CompleteIrp(Irp, status, len);
}

NTSTATUS ObserverWrite(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Write.Length;

	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS OnRegistryNotify(PVOID context, PVOID notifyClass, PVOID arg)
{
	UNREFERENCED_PARAMETER(context);

	switch ((REG_NOTIFY_CLASS)(ULONG_PTR)notifyClass) {
	case RegNtPostSetValueKey:
		auto args = (REG_POST_OPERATION_INFORMATION*)arg;
		// TODO check failed attempts too
		if (!NT_SUCCESS(args->Status))
			break;
		PCUNICODE_STRING keyName;
		if (NT_SUCCESS(CmCallbackGetKeyObjectIDEx(&g_Globals.RegCookie, args->Object, nullptr, &keyName, 0))) {
			auto preInfo = (REG_SET_VALUE_KEY_INFORMATION*)args->PreInformation;
			NT_ASSERT(preInfo);
			USHORT size = sizeof(RegistrySet);
			USHORT keyNameLen = keyName->Length + sizeof(WCHAR);
			USHORT valueNameLen = preInfo->ValueName->Length + sizeof(WCHAR);
			
			//copy only 256 bytes
			enum{MaxValueSize = 256};
			USHORT valueSize = static_cast<USHORT>(min(MaxValueSize, preInfo->DataSize));
			size += keyNameLen + valueNameLen + valueSize;
			auto info = (FullItem<RegistrySet>*)(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
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
				g_Globals.list.addItem(&info->Entry);
			} else {
				KdPrint((DRIVER_PREFIX"Failed to allocate memeory for registry set value\n"));
			}
			CmCallbackReleaseKeyObjectIDEx(keyName);
		}
		break;
	}
	return STATUS_SUCCESS;
}
