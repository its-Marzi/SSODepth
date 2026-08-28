#include <windows.h>
#include <guiddef.h>
#include <cstdio>

/*
 * MinGW defines __uuidof in a way that ReShade's generic C++ templates
 * do not accept.
 *
 * Those private-data convenience templates are not used by SSODepth,
 * but GCC still has to parse them.
 *
 * Give those unused templates a harmless GUID expression so the
 * official ReShade API headers can compile.
 */
static const GUID g_reshade_dummy_uuid = {};

#ifdef __uuidof
#undef __uuidof
#endif

#define __uuidof(T) g_reshade_dummy_uuid

#include <reshade.hpp>

extern "C" __declspec(dllexport) const char *NAME =
    "SSO Depth";

extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Experimental Star Stable Online depth workaround for Wine/OpenGL.";

static void on_reshade_begin_effects(
    reshade::api::effect_runtime *runtime,
    reshade::api::command_list *,
    reshade::api::resource_view,
    reshade::api::resource_view)
{
    static unsigned int calls = 0;
    ++calls;

    if (calls <= 5 || (calls % 600) == 0)
    {
        reshade::api::device *device =
            runtime->get_device();

        char message[256];

        std::snprintf(
            message,
            sizeof(message),
            "BEGIN EFFECTS callback works! call=%u api=%u device=%p runtime=%p",
            calls,
            static_cast<unsigned int>(device->get_api()),
            static_cast<void *>(device),
            static_cast<void *>(runtime)
        );

        reshade::log::message(
            reshade::log::level::info,
            message
        );
    }
}

BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        if (!reshade::register_addon(module))
            return FALSE;

        reshade::register_event<
            reshade::addon_event::reshade_begin_effects
        >(&on_reshade_begin_effects);

        reshade::log::message(
            reshade::log::level::info,
            "Official ReShade C++ API build loaded."
        );
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        reshade::unregister_event<
            reshade::addon_event::reshade_begin_effects
        >(&on_reshade_begin_effects);

        reshade::unregister_addon(module);
    }

    return TRUE;
}
