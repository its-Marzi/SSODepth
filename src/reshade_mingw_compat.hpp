#pragma once

#include <windows.h>
#include <guiddef.h>

// ReShade uses __uuidof in helper templates. MinGW does not implement
// MSVC's UUID machinery in the same way, so provide a stable dummy GUID
// for the private-data helper used by this add-on.
inline const GUID g_reshade_dummy_uuid = {};

#ifdef __uuidof
#undef __uuidof
#endif

#define __uuidof(T) g_reshade_dummy_uuid
