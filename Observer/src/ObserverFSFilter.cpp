#include "pch.h"
#include "ObserverFSFilter.h"
#include "FileNameInfo.h"
#include "FastMutex.h"
#include "AutoLock.h"

extern ObserverContext observerContext;

static FLT_POSTOP_CALLBACK_STATUS OnPostCreate(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, PVOID, FLT_POST_OPERATION_FLAGS flags);
static FLT_POSTOP_CALLBACK_STATUS OnPostWrite(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, PVOID, FLT_POST_OPERATION_FLAGS flags);
static FLT_PREOP_CALLBACK_STATUS OnPreWrite(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, _Outptr_ PVOID* completionContext);
static FLT_POSTOP_CALLBACK_STATUS OnPostCleanup(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, PVOID, FLT_POST_OPERATION_FLAGS flags);

static NTSTATUS ObserverUnload(_In_ FLT_FILTER_UNLOAD_FLAGS Flags);
static NTSTATUS ObserverInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType);
static NTSTATUS ObserverInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);
static VOID ObserverInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}
static VOID ObserverInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

static PFLT_FILTER filter;

// TODO Make decision what to store here
struct FileContext {
	enum { filenameBufferSize = 256}; 
	volatile LONG64 writeCounter;
	USHORT filenameSize;
	WCHAR filename[filenameBufferSize];
};

NTSTATUS ObserverFSFilterInit(PDRIVER_OBJECT DriverObject, PUNICODE_STRING regPath)
{
	NTSTATUS status;
	OBJECT_ATTRIBUTES keyAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(regPath, OBJ_KERNEL_HANDLE);
	HANDLE hRootKey = nullptr, hSubKey = nullptr, hInstanceKey = nullptr;
	UNICODE_STRING defaultInstanceStr = RTL_CONSTANT_STRING(L"DefaultInstance"), 
		altitudeStr = RTL_CONSTANT_STRING(L"Altitude"),
		flagsStr = RTL_CONSTANT_STRING(L"Flags"), subKeyName = RTL_CONSTANT_STRING(L"Instances"),
		portName = RTL_CONSTANT_STRING(L"\\ObserverPort");
	WCHAR defaultInstanceName[] = L"ObserverDefaultInstance", altitude[] = L"335342";
	enum class ResourceAcquisition{None, RootKey, SubKey, SubKeyValueSet, InstanceKey, 
		InstanceAltitude, InstanceFlags} 
	currStage = ResourceAcquisition::None;
	do {
		status = ZwOpenKey(&hRootKey, KEY_WRITE, &keyAttr);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't open root key: %wZ", regPath));
			break;
		}
		currStage = ResourceAcquisition::RootKey;

		OBJECT_ATTRIBUTES subKeyAttr;
		InitializeObjectAttributes(&subKeyAttr, &subKeyName, OBJ_KERNEL_HANDLE, hRootKey, nullptr);
		status = ZwCreateKey(&hSubKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status)){
			KdPrint((DRIVER_PREFIX "Can't open subkey Instances in: %wZ", regPath));
			break;
		}
		currStage = ResourceAcquisition::SubKey;

		status = ZwSetValueKey(hSubKey, &defaultInstanceStr, 0, REG_SZ, defaultInstanceName, 
			sizeof(defaultInstanceName));
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't set DefaultInstance value to ObserverDefaultInstance"));
			break;
		}
		currStage = ResourceAcquisition::SubKeyValueSet;

		UNICODE_STRING instanceKeyName = RTL_CONSTANT_STRING(L"ObserverDefaultInstance");
		InitializeObjectAttributes(&subKeyAttr, &instanceKeyName, OBJ_KERNEL_HANDLE, 
			hSubKey, nullptr);
		status = ZwCreateKey(&hInstanceKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't create default instance key"));
			break;
		}
		currStage = ResourceAcquisition::InstanceKey;

		status = ZwSetValueKey(hInstanceKey, &altitudeStr, 0, REG_SZ, altitude, sizeof(altitude));
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't set altitude"));
			break;
		}
		currStage = ResourceAcquisition::InstanceAltitude;

		ULONG flags = 0;
		status = ZwSetValueKey(hInstanceKey, &flagsStr, 0, REG_DWORD, &flags, sizeof(flags));
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't set flags"));
			break;
		}
		currStage = ResourceAcquisition::InstanceFlags;

		constexpr FLT_OPERATION_REGISTRATION callbacks[] = {
			{IRP_MJ_CREATE, 0, nullptr, OnPostCreate}, 
			{IRP_MJ_WRITE, 0, OnPreWrite, OnPostWrite},
			{IRP_MJ_CLEANUP, 0, nullptr, OnPostCleanup},
			{IRP_MJ_OPERATION_END}
		};
		constexpr FLT_CONTEXT_REGISTRATION context[] = {
			{FLT_FILE_CONTEXT, 0, nullptr, sizeof(FileContext), DRIVER_TAG},
			{FLT_CONTEXT_END}
		};

		const FLT_REGISTRATION reg = {
			sizeof(FLT_REGISTRATION),
			FLT_REGISTRATION_VERSION, 
			0, 
			context,
			callbacks,
			ObserverUnload,
			ObserverInstanceSetup,
			ObserverInstanceQueryTeardown,
			ObserverInstanceTeardownStart,
			ObserverInstanceTeardownComplete,
		};

		status = FltRegisterFilter(DriverObject, &reg, &filter);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Can't register filter"));
			break;
		}

		FltStartFiltering(filter);
	} while (false);
	switch (currStage) {
	case ResourceAcquisition::InstanceFlags:
		if (!NT_SUCCESS(status))
			ZwDeleteValueKey(hInstanceKey, &flagsStr);
		[[fallthrough]];
	case ResourceAcquisition::InstanceAltitude:
		if(!NT_SUCCESS(status))
			ZwDeleteValueKey(hInstanceKey, &altitudeStr);
		[[fallthrough]];
	case ResourceAcquisition::InstanceKey:
		if (!NT_SUCCESS(status))
			ZwDeleteKey(hInstanceKey);
		else
			ZwClose(hInstanceKey);
		[[fallthrough]];
	case ResourceAcquisition::SubKeyValueSet:
		if (!NT_SUCCESS(status))
			ZwDeleteValueKey(hSubKey, &defaultInstanceStr);
		[[fallthrough]];
	case ResourceAcquisition::SubKey:
		if (!NT_SUCCESS(status))
			ZwDeleteKey(hSubKey);
		else
			ZwClose(hSubKey);
		[[fallthrough]];
	case ResourceAcquisition::RootKey:
		ZwClose(hRootKey);
	}
	return status;
}

NTSTATUS ObserverUnload(_In_ FLT_FILTER_UNLOAD_FLAGS)
{
	LIST_ENTRY* entry;
	while ((entry = observerContext.ObservedEvents.RemoveItem()) != nullptr)
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Observer");
	observerContext.RegistryManager.Dispose();
	if (observerContext.RegistryRootPath.Length)
		ExFreePool(observerContext.RegistryRootPath.Buffer);
	CmUnRegisterCallback(observerContext.RegCookie);
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(observerContext.DeviceObject);

	FltUnregisterFilter(filter);
	return STATUS_SUCCESS;
}


NTSTATUS ObserverInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);

	KdPrint((DRIVER_PREFIX "ObserverInstanceSetup FS: %u\n", VolumeFilesystemType));
	return STATUS_SUCCESS;
}


NTSTATUS ObserverInstanceQueryTeardown(_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	KdPrint((DRIVER_PREFIX "ObserverInstanceQueryTeardown\n"));
	return STATUS_SUCCESS;
}

FLT_POSTOP_CALLBACK_STATUS OnPostCreate(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, 
	PVOID, FLT_POST_OPERATION_FLAGS Flags)
{
	if (Flags & FLTFL_POST_OPERATION_DRAINING)
		return FLT_POSTOP_FINISHED_PROCESSING;
	const auto& createParams = Data->Iopb->Parameters.Create;
	BOOLEAN isDirectory = FALSE;
	FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &isDirectory);
	if (isDirectory || Data->RequestorMode == KernelMode ||
		!(createParams.SecurityContext->DesiredAccess & FILE_WRITE_DATA) || Data->IoStatus.Status != STATUS_SUCCESS)
		return FLT_POSTOP_FINISHED_PROCESSING;

	FileNameInfo name(Data);
	// ignore ntfs streams
	if (!name || name->Stream.Length > 0)
		return FLT_POSTOP_FINISHED_PROCESSING;

	FileContext* fileContext, *oldContext;
	auto status = FltAllocateContext(FltObjects->Filter, FLT_FILE_CONTEXT,
		sizeof(FileContext), PagedPool, (PFLT_CONTEXT*)&fileContext);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to allocate file context (0x%08X)\n", status));
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	fileContext->writeCounter = 0;
	fileContext->filenameSize = FileContext::filenameBufferSize > name->Name.Length/sizeof(WCHAR) ?
		name->Name.Length/sizeof(WCHAR) : FileContext::filenameBufferSize;
	memcpy_s(fileContext->filename, fileContext->filenameSize * sizeof(WCHAR), name->Name.Buffer, name->Name.Length);

	status = FltSetFileContext(FltObjects->Instance,
		FltObjects->FileObject,
		FLT_SET_CONTEXT_REPLACE_IF_EXISTS,
		fileContext, (PFLT_CONTEXT*) & oldContext);

	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to set file context (0x%08X)\n", status));
		FltReleaseContext(fileContext);
		FltDeleteContext(fileContext);
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	FltReleaseContext(fileContext);
	
	// TODO get current process name for all events
	USHORT nameSize = fileContext->filenameSize + 1;
	USHORT size = sizeof(FileOpen) + (nameSize)*sizeof(WCHAR);
	auto newEvent = static_cast<FullItem<FileOpen>*>(ExAllocatePool2(POOL_FLAG_PAGED, size + sizeof(LIST_ENTRY), DRIVER_TAG));
	if (newEvent) {
		auto& eventInfo = newEvent->Data;
		KeQuerySystemTimePrecise(&eventInfo.time);
		eventInfo.type = ItemType::FileSystemOpenFile;
		eventInfo.size = size;
		eventInfo.processId = HandleToULong(PsGetCurrentProcessId());
		eventInfo.threadId = HandleToULong(PsGetCurrentThreadId());
		eventInfo.fileNameOffset = sizeof(FileOpen);
		wcsncpy_s((PWSTR)((PUCHAR)&eventInfo + sizeof(FileOpen)), 
			nameSize, 
			fileContext->filename, fileContext->filenameSize);
		observerContext.ObservedEvents.AddItem(&newEvent->Entry);
	} else {
		KdPrint(("Failed to allocate list item\n"));
	}
	if (oldContext != nullptr)
		FltReleaseContext(oldContext);
	return FLT_POSTOP_FINISHED_PROCESSING;
}
 
FLT_PREOP_CALLBACK_STATUS OnPreWrite(_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects, _Outptr_ PVOID* completionContext)
{
	UNREFERENCED_PARAMETER(Data);
	FileContext* context;
	auto status = FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, 
		(PFLT_CONTEXT*)& context);
	if (!NT_SUCCESS(status) || context == nullptr)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	*completionContext = context;
	return FLT_PREOP_SYNCHRONIZE;
}

FLT_POSTOP_CALLBACK_STATUS OnPostWrite(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, 
	PVOID contextFromPre, FLT_POST_OPERATION_FLAGS Flags)
{
	UNREFERENCED_PARAMETER(FltObjects);
	if (Flags & FLTFL_POST_OPERATION_DRAINING || Data->RequestorMode == KernelMode 
		|| Data->IoStatus.Status != STATUS_SUCCESS || contextFromPre == nullptr)
		return FLT_POSTOP_FINISHED_PROCESSING;

	FileContext* fileContext = static_cast<FileContext*>(contextFromPre);
	InterlockedIncrement64(&fileContext->writeCounter);
	
	USHORT nameSize = fileContext->filenameSize + 1;
	USHORT size = static_cast<USHORT>(sizeof(FileWrite) + (nameSize) *sizeof(WCHAR));
	auto newEvent = static_cast<FullItem<FileWrite>*>(ExAllocatePool2(POOL_FLAG_PAGED, size+ sizeof(LIST_ENTRY), DRIVER_TAG));
	if (newEvent) {
		auto& eventInfo = newEvent->Data;
		KeQuerySystemTimePrecise(&eventInfo.time);
		eventInfo.type = ItemType::FileSystemWriteFile;
		eventInfo.size = size;
		eventInfo.processId = HandleToULong(PsGetCurrentProcessId());
		eventInfo.threadId = HandleToULong(PsGetCurrentThreadId());
		eventInfo.bytesWritten = Data->Iopb->Parameters.Write.Length;
		eventInfo.fileNameOffset = sizeof(FileWrite);
		eventInfo.writeOperationCount = static_cast<ULONG>(fileContext->writeCounter);
		wcsncpy_s((PWSTR)((PUCHAR)&eventInfo + sizeof(FileWrite)), 
			nameSize, 
			fileContext->filename, fileContext->filenameSize);
		observerContext.ObservedEvents.AddItem(&newEvent->Entry);
	} else {
		KdPrint(("Failed to allocate list item\n"));
	}
	FltReleaseContext(fileContext);
	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS OnPostCleanup(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, 
	PVOID, FLT_POST_OPERATION_FLAGS flags)
{
	UNREFERENCED_PARAMETER(flags);
	UNREFERENCED_PARAMETER(Data);

	FileContext* context;
	auto status = FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&context);
	if (!NT_SUCCESS(status) || context == nullptr)
		return FLT_POSTOP_FINISHED_PROCESSING;
	FltReleaseContext(context);
	FltDeleteContext(context);
	return FLT_POSTOP_FINISHED_PROCESSING;
}
