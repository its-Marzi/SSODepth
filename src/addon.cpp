#include <windows.h>
#include <guiddef.h>
#include <GL/gl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>

/*
 * MinGW compatibility shim.
 *
 * ReShade's public headers contain MSVC-oriented __uuidof convenience
 * templates. SSODepth does not use those templates, but GCC still needs
 * to parse them.
 */
static const GUID g_reshade_dummy_uuid = {};

#ifdef __uuidof
#undef __uuidof
#endif

#define __uuidof(T) g_reshade_dummy_uuid

#include <reshade.hpp>

#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif

#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif

#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#endif

#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#endif

extern "C" __declspec(dllexport) const char *NAME =
    "SSO Depth";

extern "C" __declspec(dllexport) const char *AUTHOR =
    "Marzi";

extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Provides Star Stable Online's native OpenGL scene depth texture "
    "to ReShade effects under Wine.";

extern "C" __declspec(dllexport) const char *WEBSITE =
    "https://github.com/its-Marzi/SSODepth";

extern "C" __declspec(dllexport) const char *ISSUES =
    "https://github.com/its-Marzi/SSODepth/issues";

using BindFramebufferFn =
    void (APIENTRY *)(GLenum target, GLuint framebuffer);

using GetFramebufferAttachmentParameterivFn =
    void (APIENTRY *)(
        GLenum target,
        GLenum attachment,
        GLenum pname,
        GLint *params);

/*
 * Most recently observed full-resolution scene framebuffer.
 *
 * OpenGL object names are not stable between launches, so this is
 * detected dynamically rather than hard-coded.
 */
static std::atomic<GLuint> g_scene_fbo { 0 };
static std::atomic<std::uint32_t> g_scene_width { 0 };
static std::atomic<std::uint32_t> g_scene_height { 0 };

/*
 * Map native OpenGL texture object IDs to ReShade's own resource_view
 * handles. Using ReShade's tracked handle means we do not need to guess
 * how to construct a resource_view ourselves.
 */
static std::unordered_map<std::uint32_t, std::uint64_t> g_texture_views;
static std::mutex g_texture_views_mutex;

/*
 * Used only to avoid repeating the same informational log message
 * every frame.
 */
static std::atomic<std::uint64_t> g_last_bound_pair { 0 };

static void detect_scene_fbo()
{
    GLint viewport[4] = {};
    GLint draw_fbo = 0;
    GLint depth_bits = 0;
    GLboolean depth_write = GL_FALSE;

    glGetIntegerv(
        GL_VIEWPORT,
        viewport);

    glGetIntegerv(
        GL_DRAW_FRAMEBUFFER_BINDING,
        &draw_fbo);

    glGetIntegerv(
        GL_DEPTH_BITS,
        &depth_bits);

    glGetBooleanv(
        GL_DEPTH_WRITEMASK,
        &depth_write);

    const bool depth_test =
        glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;

    /*
     * SSO's main world pass has consistently presented as:
     *
     *   - non-zero FBO
     *   - full-resolution viewport
     *   - depth testing enabled
     *   - depth writes enabled
     *   - 24/32-bit depth
     *
     * Requiring depth writes prevents later full-resolution
     * post-processing passes from replacing the scene candidate.
     */
    if (
        draw_fbo != 0 &&
        viewport[2] >= 1280 &&
        viewport[3] >= 720 &&
        depth_bits >= 24 &&
        depth_test &&
        depth_write == GL_TRUE)
    {
        g_scene_fbo.store(
            static_cast<GLuint>(draw_fbo),
            std::memory_order_relaxed);

        g_scene_width.store(
            static_cast<std::uint32_t>(viewport[2]),
            std::memory_order_relaxed);

        g_scene_height.store(
            static_cast<std::uint32_t>(viewport[3]),
            std::memory_order_relaxed);
    }
}

static bool on_draw(
    reshade::api::command_list *,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t)
{
    detect_scene_fbo();
    return false;
}

static bool on_draw_indexed(
    reshade::api::command_list *,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::int32_t,
    std::uint32_t)
{
    detect_scene_fbo();
    return false;
}

static void on_init_resource_view(
    reshade::api::device *device,
    reshade::api::resource,
    reshade::api::resource_usage,
    const reshade::api::resource_view_desc &,
    reshade::api::resource_view view)
{
    if (
        device->get_api() !=
        reshade::api::device_api::opengl)
    {
        return;
    }

    /*
     * ReShade's OpenGL resource_view encoding contains the native
     * OpenGL object ID in the lower 32 bits.
     */
    const std::uint32_t object =
        static_cast<std::uint32_t>(
            view.handle & 0xFFFFFFFFull);

    const std::uint32_t target =
        static_cast<std::uint32_t>(
            view.handle >> 40);

    if (object == 0 || target == 0)
        return;

    std::lock_guard<std::mutex> lock(
        g_texture_views_mutex);

    g_texture_views[object] =
        view.handle;
}

static void on_destroy_resource_view(
    reshade::api::device *device,
    reshade::api::resource_view view)
{
    if (
        device->get_api() !=
        reshade::api::device_api::opengl)
    {
        return;
    }

    const std::uint32_t object =
        static_cast<std::uint32_t>(
            view.handle & 0xFFFFFFFFull);

    if (object == 0)
        return;

    std::lock_guard<std::mutex> lock(
        g_texture_views_mutex);

    auto it =
        g_texture_views.find(object);

    if (
        it != g_texture_views.end() &&
        it->second == view.handle)
    {
        g_texture_views.erase(it);
    }
}

static void on_reshade_begin_effects(
    reshade::api::effect_runtime *runtime,
    reshade::api::command_list *,
    reshade::api::resource_view,
    reshade::api::resource_view)
{
    const GLuint scene_fbo =
        g_scene_fbo.load(
            std::memory_order_relaxed);

    const std::uint32_t scene_width =
        g_scene_width.load(
            std::memory_order_relaxed);

    const std::uint32_t scene_height =
        g_scene_height.load(
            std::memory_order_relaxed);

    if (scene_fbo == 0)
        return;

    /*
     * Verify that our candidate matches the current ReShade runtime
     * dimensions. This prevents a similarly shaped auxiliary buffer
     * from being exposed as DEPTH after a resolution change.
     */
    std::uint32_t runtime_width = 0;
    std::uint32_t runtime_height = 0;

    runtime->get_screenshot_width_and_height(
        &runtime_width,
        &runtime_height);

    if (
        scene_width != runtime_width ||
        scene_height != runtime_height)
    {
        return;
    }

    auto bind_framebuffer =
        reinterpret_cast<BindFramebufferFn>(
            wglGetProcAddress(
                "glBindFramebuffer"));

    auto get_attachment =
        reinterpret_cast<GetFramebufferAttachmentParameterivFn>(
            wglGetProcAddress(
                "glGetFramebufferAttachmentParameteriv"));

    if (
        bind_framebuffer == nullptr ||
        get_attachment == nullptr)
    {
        return;
    }

    GLint previous_read_fbo = 0;

    glGetIntegerv(
        GL_READ_FRAMEBUFFER_BINDING,
        &previous_read_fbo);

    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        scene_fbo);

    GLint object_type = 0;
    GLint object_name = 0;

    get_attachment(
        GL_READ_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
        &object_type);

    get_attachment(
        GL_READ_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
        &object_name);

    /*
     * Restore SSO/ReShade's OpenGL state before doing anything else.
     */
    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(
            previous_read_fbo));

    /*
     * We deliberately only support a native texture here.
     *
     * GL_TEXTURE == 0x1702.
     */
    if (
        object_type != GL_TEXTURE ||
        object_name <= 0)
    {
        return;
    }

    reshade::api::resource_view depth_view = {};

    {
        std::lock_guard<std::mutex> lock(
            g_texture_views_mutex);

        const auto it =
            g_texture_views.find(
                static_cast<std::uint32_t>(
                    object_name));

        if (it == g_texture_views.end())
            return;

        depth_view.handle =
            it->second;
    }

    /*
     * Expose SSO's real scene depth to every ReShade effect that uses
     * the standard DEPTH semantic.
     */
    runtime->update_texture_bindings(
        "DEPTH",
        depth_view,
        depth_view);

    /*
     * Log only when the framebuffer/depth texture pair changes.
     */
    const std::uint64_t pair =
        (static_cast<std::uint64_t>(
            scene_fbo) << 32) |
        static_cast<std::uint32_t>(
            object_name);

    const std::uint64_t previous_pair =
        g_last_bound_pair.exchange(
            pair,
            std::memory_order_relaxed);

    if (pair != previous_pair)
    {
        char message[384];

        std::snprintf(
            message,
            sizeof(message),
            "Active depth bridge: %ux%u, FBO %u, "
            "OpenGL texture %d.",
            scene_width,
            scene_height,
            scene_fbo,
            object_name);

        reshade::log::message(
            reshade::log::level::info,
            message);
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
            reshade::addon_event::init_resource_view
        >(&on_init_resource_view);

        reshade::register_event<
            reshade::addon_event::destroy_resource_view
        >(&on_destroy_resource_view);

        reshade::register_event<
            reshade::addon_event::draw
        >(&on_draw);

        reshade::register_event<
            reshade::addon_event::draw_indexed
        >(&on_draw_indexed);

        reshade::register_event<
            reshade::addon_event::reshade_begin_effects
        >(&on_reshade_begin_effects);

        reshade::log::message(
            reshade::log::level::info,
            "SSO Depth loaded.");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        /*
         * ReShade unregisters callbacks belonging to this add-on when
         * the add-on itself is unregistered.
         */
        reshade::unregister_addon(module);
    }

    return TRUE;
}
