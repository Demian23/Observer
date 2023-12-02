#include "pch.h"
#include <ntddk.h>
#include "List.h"
#include "AutoLock.h"
#include "../../Client/src/ObserverClientInfo.h"

void List::Init(ULONG _maxCount)
{
	InitializeListHead(&head);
	maxCount = _maxCount;
	count = 0;
	lock.Init();
}

void List::AddItem(LIST_ENTRY* item)
{
	AutoLock locker(lock);

	if (count == maxCount) {
		auto listHead = RemoveHeadList(&head);
		ExFreePool(CONTAINING_RECORD(listHead, FullItem<ItemHeader>, Entry));
		count--;
	}
	InsertTailList(&head, item);
	count++;
}

LIST_ENTRY* List::RemoveItem()
{
	AutoLock locker(lock);
	auto item = RemoveHeadList(&head);
	if (item == &head)
		return nullptr;
	count--;
	return item;
}