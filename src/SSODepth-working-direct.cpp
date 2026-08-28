#include <windows.h>
#include <guiddef.h>
#include <GL/gl.h>

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <mutex>

/*
 * MinGW compatibility shim for ReShade's MSVC-oriented __uuidof
 * convenience templates.
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

extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Star Stable Online depth bridge for ReShade on Wine/OpenGL.";

using BindFramebufferFn =
    void (APIENTRY *)(GLenum target, GLuint framebuffer);

using GetFramebufferAttachmentParameterivFn =
    void (APIENTRY *)(
        GLenum target,
        GLenum attachment,
        GLenum pname,
        GLint *params);

static GLuint g_scene_fbo = 0;
static unsigned int g_scene_width = 0;
static unsigned int g_scene_height = 0;

/*
 * ReShade already knows about many OpenGL texture views.
 *
 * Remember the exact native ReShade handle for each OpenGL texture ID,
 * so we do not have to guess the texture target if ReShade saw it.
 */
static std::unordered_map<std::uint32_t, std::uint64_t> g_texture_views;
static std::mutex g_texture_views_mutex;

static std::uint64_t g_begin_effects_calls = 0;

static void detect_scene_fbo()
{
    GLint viewport[4] = {};
    GLint draw_fbo = 0;
    GLint depth_bits = 0;

    glGetIntegerv(
        GL_VIEWPORT,
        viewport);

    glGetIntegerv(
        GL_DRAW_FRAMEBUFFER_BINDING,
        &draw_fbo);

    glGetIntegerv(
        GL_DEPTH_BITS,
        &depth_bits);

    const bool depth_test =
        glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;

    /*
     * This is the pattern we observed for SSO's main world pass:
     *
     * - full-resolution viewport
     * - non-zero FBO
     * - real depth buffer
     * - depth testing active
     *
     * It excludes the 1024x1024 auxiliary/shadow pass and
     * ReShade's own depth-less effect framebuffer.
     */
    if (
        draw_fbo != 0 &&
        viewport[2] >= 1280 &&
        viewport[3] >= 720 &&
        depth_bits >= 24 &&
        depth_test)
    {
        g_scene_fbo =
            static_cast<GLuint>(draw_fbo);

        g_scene_width =
            static_cast<unsigned int>(viewport[2]);

        g_scene_height =
            static_cast<unsigned int>(viewport[3]);
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
    ++g_begin_effects_calls;

    if (g_scene_fbo == 0)
        return;

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
        g_scene_fbo);

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
     * Restore the GL state immediately.
     */
    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previous_read_fbo));

    /*
     * We can only directly sample an actual texture.
     *
     * 0x1702 == GL_TEXTURE.
     */
    if (
        object_type != GL_TEXTURE ||
        object_name <= 0)
    {
        return;
    }

    reshade::api::resource_view depth_view = {};

    bool used_tracked_view = false;

    /*
     * Prefer the exact OpenGL view handle ReShade observed when
     * the texture was created.
     */
    {
        std::lock_guard<std::mutex> lock(
            g_texture_views_mutex);

        auto it =
            g_texture_views.find(
                static_cast<std::uint32_t>(
                    object_name));

        if (it != g_texture_views.end())
        {
            depth_view.handle = it->second;
            used_tracked_view = true;
        }
    }

    /*
     * Fallback:
     *
     * ReShade documents OpenGL resource_view handles as:
     *
     *   upper 24 bits = OpenGL target
     *   lower 32 bits = OpenGL object ID
     *
     * SSO's full-resolution depth attachment is expected to be
     * a normal GL_TEXTURE_2D.
     */
    if (depth_view.handle == 0)
    {
        depth_view.handle =
            (static_cast<std::uint64_t>(
                GL_TEXTURE_2D) << 40) |
            static_cast<std::uint32_t>(
                object_name);
    }

    /*
     * THIS IS THE ACTUAL BRIDGE.
     *
     * Every ReShade FX texture declared with the DEPTH semantic
     * should now point at SSO's scene depth texture.
     */
    runtime->update_texture_bindings(
        "DEPTH",
        depth_view,
        depth_view);

    if (
        g_begin_effects_calls <= 10 ||
        (g_begin_effects_calls % 600) == 0)
    {
        const std::uint32_t target =
            static_cast<std::uint32_t>(
                depth_view.handle >> 40);

        char message[512];

        std::snprintf(
            message,
            sizeof(message),
            "DEPTH BRIDGE | sceneFBO=%u | size=%ux%u | "
            "texture=%d | view=0x%llX | target=0x%X | "
            "source=%s",
            g_scene_fbo,
            g_scene_width,
            g_scene_height,
            object_name,
            static_cast<unsigned long long>(
                depth_view.handle),
            target,
            used_tracked_view
                ? "tracked"
                : "fallback-GL_TEXTURE_2D");

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
            "SSO direct depth bridge loaded.");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        reshade::unregister_event<
            reshade::addon_event::reshade_begin_effects
        >(&on_reshade_begin_effects);

        reshade::unregister_event<
            reshade::addon_event::draw_indexed
        >(&on_draw_indexed);

        reshade::unregister_event<
            reshade::addon_event::draw
        >(&on_draw);

        reshade::unregister_event<
            reshade::addon_event::destroy_resource_view
        >(&on_destroy_resource_view);

        reshade::unregister_event<
            reshade::addon_event::init_resource_view
        >(&on_init_resource_view);

        reshade::unregister_addon(module);
    }

    return TRUE;
}
