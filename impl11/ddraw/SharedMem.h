#pragma once

#include <windows.h>

constexpr float POVOffsetIncr = 0.025f;

// This is the actual data that will be shared across DLLs. It contains
// a mixture of custom fields and a generic pointer to share whatever else
// we need later.
struct SharedData {
	// Offset added to the current POV when VR is active. This is controlled by ddraw
	float POVOffsetX, POVOffsetY, POVOffsetZ;
	void *pDataPtr;
	// Euler angles for the current camera matrix coming from SteamVR. This is writen by
	// CockpitLookHook:
	float Yaw, Pitch, Roll;
};

// This is a proxy to share data between the hook and ddraw.
// This proxy should only contain two fields:
// - bDataReady is set to true once the writer has initialized
//   the proxy structure.
// - pDataPtr points to the actual data to be shared in the regular
//   heap space
struct SharedDataProxy {
	bool bDataReady;
	SharedData *pSharedData;
};

constexpr auto SHARED_MEM_NAME = "Local\\CockpitLookHook";
constexpr auto SHARED_MEM_SIZE = sizeof(SharedDataProxy);

// This is the shared memory handler. Call GetMemoryPtr to get a pointer
// to a SharedData structure
class SharedMem {
private:
	HANDLE hMapFile;
	void *pSharedMemPtr;
	bool InitMemory(bool OpenCreate);

public:
	// Specify true in OpenCreate to create the shared memory handle. Set it to false to
	// open an existing shared memory handle.
	SharedMem(bool OpenCreate);
	~SharedMem();
	void *GetMemoryPtr();
};
