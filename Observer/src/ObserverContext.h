#pragma once

#include "List.h"
#include "ObserverRegistryManager.h"

struct ObserverContext final{
	List RegistryNotifications;
	ObserverRegistryManager RegistryManager;
	LARGE_INTEGER RegCookie;
};
