#include <windows.h>
#include <guiddef.h>
#include <GL/gl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>


// ReShade uses __uuidof in a few helper templates, which MinGW does not
// handle normally. SSODepth does not use those templates, so a dummy GUID
// is enough to let the header compile.
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


extern "C" __declspec(dllexport) const char *NAME = "SSO Depth";
extern "C" __declspec(dllexport) const char *AUTHOR = "Marzi";

extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Provides Star Stable Online's native OpenGL scene depth texture "
    "to ReShade effects.";

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


// Most recently detected framebuffer used for SSO's main 3D scene.
static std::atomic<GLuint> g_scene_fbo { 0 };
static std::atomic<std::uint32_t> g_scene_width { 0 };
static std::atomic<std::uint32_t> g_scene_height { 0 };


// Map OpenGL texture IDs to the resource views ReShade created for them.
static std::unordered_map<std::uint32_t, std::uint64_t> g_texture_views;
static std::mutex g_texture_views_mutex;


// Used to avoid writing the same depth-buffer message every frame.
static std::atomic<std::uint64_t> g_last_depth_key { 0 };


static void detect_scene_framebuffer()
{
    GLint viewport[4] = {};
    GLint draw_fbo = 0;
    GLint depth_bits = 0;
    GLboolean depth_write = GL_FALSE;

    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_DEPTH_BITS, &depth_bits);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);

    const bool depth_test = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;

    // SSO's main scene pass uses a large framebuffer with depth testing
    // and depth writes enabled. Smaller render passes are ignored.
    if (draw_fbo != 0 &&
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
    detect_scene_framebuffer();
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
    detect_scene_framebuffer();
    return false;
}


static void on_init_resource_view(
    reshade::api::device *device,
    reshade::api::resource,
    reshade::api::resource_usage,
    const reshade::api::resource_view_desc &,
    reshade::api::resource_view view)
{
    if (device->get_api() != reshade::api::device_api::opengl)
        return;

    // ReShade stores the native OpenGL object ID in the low 32 bits
    // of an OpenGL resource-view handle.
    const std::uint32_t texture_id =
        static_cast<std::uint32_t>(view.handle & 0xFFFFFFFFull);

    const std::uint32_t gl_target =
        static_cast<std::uint32_t>(view.handle >> 40);

    if (texture_id == 0 || gl_target == 0)
        return;

    std::lock_guard<std::mutex> lock(g_texture_views_mutex);
    g_texture_views[texture_id] = view.handle;
}


static void on_destroy_resource_view(
    reshade::api::device *device,
    reshade::api::resource_view view)
{
    if (device->get_api() != reshade::api::device_api::opengl)
        return;

    const std::uint32_t texture_id =
        static_cast<std::uint32_t>(view.handle & 0xFFFFFFFFull);

    if (texture_id == 0)
        return;

    std::lock_guard<std::mutex> lock(g_texture_views_mutex);

    const auto it = g_texture_views.find(texture_id);

    if (it != g_texture_views.end() && it->second == view.handle)
        g_texture_views.erase(it);
}


static void on_reshade_begin_effects(
    reshade::api::effect_runtime *runtime,
    reshade::api::command_list *,
    reshade::api::resource_view,
    reshade::api::resource_view)
{
    const GLuint scene_fbo =
        g_scene_fbo.load(std::memory_order_relaxed);

    const std::uint32_t scene_width =
        g_scene_width.load(std::memory_order_relaxed);

    const std::uint32_t scene_height =
        g_scene_height.load(std::memory_order_relaxed);

    if (scene_fbo == 0)
        return;


    // Make sure the scene buffer still matches ReShade's current output size.
    std::uint32_t runtime_width = 0;
    std::uint32_t runtime_height = 0;

    runtime->get_screenshot_width_and_height(
        &runtime_width,
        &runtime_height);

    if (scene_width != runtime_width || scene_height != runtime_height)
        return;


    auto bind_framebuffer =
        reinterpret_cast<BindFramebufferFn>(
            wglGetProcAddress("glBindFramebuffer"));

    auto get_attachment =
        reinterpret_cast<GetFramebufferAttachmentParameterivFn>(
            wglGetProcAddress("glGetFramebufferAttachmentParameteriv"));

    if (bind_framebuffer == nullptr || get_attachment == nullptr)
        return;


    GLint previous_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);


    // Ask OpenGL which texture is attached as depth to the scene framebuffer.
    bind_framebuffer(GL_READ_FRAMEBUFFER, scene_fbo);

    GLint object_type = 0;
    GLint texture_id = 0;

    get_attachment(
        GL_READ_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
        &object_type);

    get_attachment(
        GL_READ_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
        &texture_id);


    // Restore the framebuffer ReShade had bound before the query.
    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previous_read_fbo));

    if (object_type != GL_TEXTURE || texture_id <= 0)
        return;


    reshade::api::resource_view depth_view = {};

    {
        std::lock_guard<std::mutex> lock(g_texture_views_mutex);

        const auto it =
            g_texture_views.find(static_cast<std::uint32_t>(texture_id));

        if (it == g_texture_views.end())
            return;

        depth_view.handle = it->second;
    }


    // Give ReShade effects SSO's depth texture as their normal DEPTH input.
    runtime->update_texture_bindings(
        "DEPTH",
        depth_view,
        depth_view);


    // Only log when SSO switches to another framebuffer or depth texture.
    const std::uint64_t depth_key =
        (static_cast<std::uint64_t>(scene_fbo) << 32) |
        static_cast<std::uint32_t>(texture_id);

    const std::uint64_t previous_depth_key =
        g_last_depth_key.exchange(
            depth_key,
            std::memory_order_relaxed);

    if (depth_key != previous_depth_key)
    {
        char message[384];

        std::snprintf(
            message,
            sizeof(message),
            "Active depth bridge: %ux%u, FBO %u, OpenGL texture %d.",
            scene_width,
            scene_height,
            scene_fbo,
            texture_id);

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

        reshade::register_event<reshade::addon_event::init_resource_view>(
            &on_init_resource_view);

        reshade::register_event<reshade::addon_event::destroy_resource_view>(
            &on_destroy_resource_view);

        reshade::register_event<reshade::addon_event::draw>(
            &on_draw);

        reshade::register_event<reshade::addon_event::draw_indexed>(
            &on_draw_indexed);

        reshade::register_event<reshade::addon_event::reshade_begin_effects>(
            &on_reshade_begin_effects);

        reshade::log::message(
            reshade::log::level::info,
            "SSO Depth loaded.");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // unregister_addon also removes this add-on's event callbacks.
        reshade::unregister_addon(module);
    }

    return TRUE;
}
