#include "reshade_mingw_compat.hpp"

#include <GL/gl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#include <imgui.h>
#include <reshade.hpp>
#include "state_tracking.hpp"


#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif

#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB 0x80C8
#endif

#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB 0x80C9
#endif

#ifndef GL_FRAMEBUFFER_DEFAULT
#define GL_FRAMEBUFFER_DEFAULT 0x8218
#endif

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
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
    "Provides Star Stable Online's native OpenGL depth texture "
    "to ReShade effects.";

extern "C" __declspec(dllexport) const char *WEBSITE =
    "https://github.com/its-Marzi/SSODepth";

extern "C" __declspec(dllexport) const char *ISSUES =
    "https://github.com/its-Marzi/SSODepth/issues";


static constexpr const char *SSODEPTH_VERSION = "0.3.0-dev";

static char g_addon_path[MAX_PATH] = {};
static char g_executable_path[MAX_PATH] = {};


using BindFramebufferFn =
    void (APIENTRY *)(GLenum target, GLuint framebuffer);

using GetFramebufferAttachmentParameterivFn =
    void (APIENTRY *)(
        GLenum target,
        GLenum attachment,
        GLenum pname,
        GLint *params);

using TexStorage2DFn =
    void (APIENTRY *)(
        GLenum target,
        GLsizei levels,
        GLenum internal_format,
        GLsizei width,
        GLsizei height);


// Most recently detected framebuffer used for SSO's main 3D scene.
static std::atomic<GLuint> g_scene_fbo { 0 };
static std::atomic<std::uint32_t> g_scene_width { 0 };
static std::atomic<std::uint32_t> g_scene_height { 0 };
static std::atomic<std::uint32_t> g_output_width { 0 };
static std::atomic<std::uint32_t> g_output_height { 0 };

// ReShade runtime and current render target used for early effect rendering.
static std::atomic<reshade::api::effect_runtime *> g_runtime { nullptr };
static std::atomic<std::uint64_t> g_current_rtv_handle { 0 };
static std::atomic<bool> g_effects_rendered_this_frame { false };
static std::atomic<bool> g_logged_early_effects { false };

// Upright intermediate color target for early effect rendering.
// Its storage is created directly in OpenGL, while ReShade creates
// the sRGB view used for correct color-space handling.
static GLuint g_early_effects_texture = 0;
static std::uint32_t g_early_effects_width = 0;
static std::uint32_t g_early_effects_height = 0;
static reshade::api::device *g_early_effects_device = nullptr;
static reshade::api::resource g_early_effects_resource = {};
static reshade::api::resource_view g_early_effects_rtv = {};
static reshade::api::resource_view g_early_effects_rtv_srgb = {};

// Map OpenGL texture IDs to the resource views ReShade created for them.
static std::unordered_map<std::uint32_t, std::uint64_t> g_texture_views;
static std::mutex g_texture_views_mutex;


// Used to avoid writing the same depth bridge message every frame.
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

    const std::uint32_t output_width =
        g_output_width.load(std::memory_order_relaxed);

    const std::uint32_t output_height =
        g_output_height.load(std::memory_order_relaxed);

    if (output_width == 0 || output_height == 0)
        return;

    const bool full_output =
        static_cast<std::uint32_t>(viewport[2]) == output_width &&
        static_cast<std::uint32_t>(viewport[3]) == output_height;

    // SSO's main scene pass matches the current output size and writes depth.
    // This filters out smaller auxiliary render passes.
    if (draw_fbo != 0 &&
        full_output &&
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


static reshade::api::resource make_opengl_resource(
    GLenum target,
    GLuint object)
{
    reshade::api::resource resource = {};

    resource.handle =
        (static_cast<std::uint64_t>(target) << 40) |
        static_cast<std::uint64_t>(object);

    return resource;
}


static reshade::api::resource_view make_opengl_resource_view(
    GLenum target,
    GLuint object)
{
    reshade::api::resource_view view = {};

    view.handle =
        (static_cast<std::uint64_t>(target) << 40) |
        static_cast<std::uint64_t>(object);

    return view;
}


static reshade::api::resource get_opengl_back_buffer_resource()
{
    return make_opengl_resource(
        GL_FRAMEBUFFER_DEFAULT,
        GL_BACK);
}


static void destroy_early_effects_target()
{
    // The sRGB view was created through the ReShade API, so let
    // ReShade destroy and unregister that view too.
    if (g_early_effects_rtv_srgb != 0 &&
        g_early_effects_device != nullptr)
    {
        g_early_effects_device->destroy_resource_view(
            g_early_effects_rtv_srgb);
    }

    g_early_effects_rtv_srgb = {};

    if (g_early_effects_texture != 0)
    {
        glDeleteTextures(
            1,
            &g_early_effects_texture);

        g_early_effects_texture = 0;
    }

    g_early_effects_width = 0;
    g_early_effects_height = 0;
    g_early_effects_device = nullptr;
    g_early_effects_resource = {};
    g_early_effects_rtv = {};
}


static bool ensure_early_effects_target(
    reshade::api::effect_runtime *runtime)
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    runtime->get_screenshot_width_and_height(
        &width,
        &height);

    if (width == 0 || height == 0)
        return false;

    if (g_early_effects_texture != 0 &&
        g_early_effects_rtv_srgb != 0 &&
        g_early_effects_width == width &&
        g_early_effects_height == height)
    {
        return true;
    }

    destroy_early_effects_target();

    const TexStorage2DFn tex_storage_2d =
        reinterpret_cast<TexStorage2DFn>(
            wglGetProcAddress("glTexStorage2D"));

    if (tex_storage_2d == nullptr)
    {
        reshade::log::message(
            reshade::log::level::error,
            "Required OpenGL glTexStorage2D function is unavailable.");

        return false;
    }

    GLint previous_texture = 0;

    glGetIntegerv(
        GL_TEXTURE_BINDING_2D,
        &previous_texture);

    while (glGetError() != GL_NO_ERROR)
    {
    }

    GLuint texture = 0;

    glGenTextures(
        1,
        &texture);

    if (texture == 0)
        return false;

    glBindTexture(
        GL_TEXTURE_2D,
        texture);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_NEAREST);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_NEAREST);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_BASE_LEVEL,
        0);

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAX_LEVEL,
        0);

    tex_storage_2d(
        GL_TEXTURE_2D,
        1,
        GL_RGBA8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height));

    const GLenum storage_error =
        glGetError();

    glBindTexture(
        GL_TEXTURE_2D,
        static_cast<GLuint>(previous_texture));

    if (storage_error != GL_NO_ERROR)
    {
        glDeleteTextures(
            1,
            &texture);

        reshade::log::message(
            reshade::log::level::error,
            "Failed to allocate the early-effects OpenGL texture.");

        return false;
    }

    const reshade::api::resource resource =
        make_opengl_resource(
            GL_TEXTURE_2D,
            texture);

    const reshade::api::resource_view linear_rtv =
        make_opengl_resource_view(
            GL_TEXTURE_2D,
            texture);

    reshade::api::device *const device =
        runtime->get_device();

    reshade::api::resource_view srgb_rtv = {};

    if (!device->create_resource_view(
            resource,
            reshade::api::resource_usage::render_target,
            reshade::api::resource_view_desc(
                reshade::api::format::r8g8b8a8_unorm_srgb),
            &srgb_rtv))
    {
        glDeleteTextures(
            1,
            &texture);

        reshade::log::message(
            reshade::log::level::error,
            "Failed to create the early-effects sRGB render-target view.");

        return false;
    }

    g_early_effects_texture = texture;
    g_early_effects_width = width;
    g_early_effects_height = height;
    g_early_effects_device = device;
    g_early_effects_resource = resource;
    g_early_effects_rtv = linear_rtv;
    g_early_effects_rtv_srgb = srgb_rtv;

    return true;
}


static void render_effects_before_ui(
    reshade::api::command_list *cmd_list,
    std::uint32_t vertex_count)
{
    if (vertex_count != 4 ||
        g_effects_rendered_this_frame.load(std::memory_order_relaxed))
    {
        return;
    }

    reshade::api::effect_runtime *const runtime =
        g_runtime.load(std::memory_order_relaxed);

    if (runtime == nullptr ||
        runtime->get_device() != cmd_list->get_device())
    {
        return;
    }

    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;

    runtime->get_screenshot_width_and_height(
        &output_width,
        &output_height);

    if (output_width == 0 || output_height == 0)
        return;

    GLint viewport[4] = {};
    GLint draw_fbo = 0;
    GLint blend_src = 0;
    GLint blend_dst = 0;
    GLboolean depth_write = GL_FALSE;

    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);

    const bool depth_test =
        glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;

    const bool blend =
        glIsEnabled(GL_BLEND) == GL_TRUE;

    // SSO's final UI compositing draw targets the default framebuffer,
    // covers the full output, disables depth, and uses GL_ONE /
    // GL_SRC_ALPHA blending.
    if (draw_fbo != 0 ||
        viewport[0] != 0 ||
        viewport[1] != 0 ||
        static_cast<std::uint32_t>(viewport[2]) != output_width ||
        static_cast<std::uint32_t>(viewport[3]) != output_height ||
        depth_test ||
        depth_write != GL_FALSE ||
        !blend ||
        blend_src != GL_ONE ||
        blend_dst != GL_SRC_ALPHA)
    {
        return;
    }

    if (g_early_effects_texture == 0)
        return;

    state_tracking *const current_state =
        cmd_list->get_private_data<state_tracking>();

    if (current_state == nullptr)
        return;

    const reshade::api::resource back_buffer =
        get_opengl_back_buffer_resource();

    // ReShade's OpenGL copy implementation flips Y whenever the
    // default framebuffer is one side of the copy. This turns SSO's
    // native framebuffer into an upright texture for effect sampling.
    cmd_list->copy_texture_region(
        back_buffer,
        0,
        nullptr,
        g_early_effects_resource,
        0,
        nullptr);

    // Mark the frame first, since ReShade's own passes also issue draws.
    g_effects_rendered_this_frame.store(
        true,
        std::memory_order_relaxed);

    if (!g_logged_early_effects.exchange(
            true,
            std::memory_order_relaxed))
    {
        reshade::log::message(
            reshade::log::level::info,
            "Rendering ReShade effects before SSO UI composite.");
    }

    runtime->render_effects(
        cmd_list,
        g_early_effects_rtv,
        g_early_effects_rtv_srgb);

    // Copy the processed image back to SSO. Since the destination is
    // the default framebuffer, ReShade performs the opposite Y flip.
    cmd_list->copy_texture_region(
        g_early_effects_resource,
        0,
        nullptr,
        back_buffer,
        0,
        nullptr);

    // Restore the state SSO prepared for its UI composite.
    current_state->apply(cmd_list);
}


static bool on_draw(
    reshade::api::command_list *cmd_list,
    std::uint32_t vertex_count,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t)
{
    detect_scene_framebuffer();
    render_effects_before_ui(cmd_list, vertex_count);

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


static bool get_framebuffer_texture(
    GLuint framebuffer,
    GLenum attachment,
    GLint &texture_id)
{
    auto bind_framebuffer =
        reinterpret_cast<BindFramebufferFn>(
            wglGetProcAddress("glBindFramebuffer"));

    auto get_attachment =
        reinterpret_cast<GetFramebufferAttachmentParameterivFn>(
            wglGetProcAddress("glGetFramebufferAttachmentParameteriv"));

    if (bind_framebuffer == nullptr || get_attachment == nullptr)
        return false;

    GLint previous_read_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);

    bind_framebuffer(GL_READ_FRAMEBUFFER, framebuffer);

    GLint object_type = 0;
    texture_id = 0;

    get_attachment(
        GL_READ_FRAMEBUFFER,
        attachment,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
        &object_type);

    get_attachment(
        GL_READ_FRAMEBUFFER,
        attachment,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
        &texture_id);

    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previous_read_fbo));

    return object_type == GL_TEXTURE && texture_id > 0;
}


static reshade::api::resource_view find_texture_view(GLint texture_id)
{
    reshade::api::resource_view view = {};

    std::lock_guard<std::mutex> lock(g_texture_views_mutex);

    const auto it =
        g_texture_views.find(static_cast<std::uint32_t>(texture_id));

    if (it != g_texture_views.end())
        view.handle = it->second;

    return view;
}


static void update_depth_binding(
    reshade::api::effect_runtime *runtime,
    std::uint32_t runtime_width,
    std::uint32_t runtime_height)
{
    const GLuint scene_fbo =
        g_scene_fbo.load(std::memory_order_relaxed);

    const std::uint32_t scene_width =
        g_scene_width.load(std::memory_order_relaxed);

    const std::uint32_t scene_height =
        g_scene_height.load(std::memory_order_relaxed);

    if (scene_fbo == 0 ||
        scene_width != runtime_width ||
        scene_height != runtime_height)
    {
        return;
    }

    GLint texture_id = 0;

    if (!get_framebuffer_texture(
            scene_fbo,
            GL_DEPTH_ATTACHMENT,
            texture_id))
    {
        return;
    }

    const reshade::api::resource_view depth_view =
        find_texture_view(texture_id);

    if (depth_view.handle == 0)
        return;

    // Give ReShade effects SSO's depth texture as their normal DEPTH input.
    runtime->update_texture_bindings(
        "DEPTH",
        depth_view,
        depth_view);

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




static void on_init_effect_runtime(
    reshade::api::effect_runtime *runtime)
{
    if (runtime->get_device()->get_api() !=
        reshade::api::device_api::opengl)
    {
        return;
    }

    g_runtime.store(
        runtime,
        std::memory_order_relaxed);
}


static void on_destroy_effect_runtime(
    reshade::api::effect_runtime *runtime)
{
    if (g_runtime.load(std::memory_order_relaxed) == runtime)
    {
        destroy_early_effects_target();

        g_runtime.store(
            nullptr,
            std::memory_order_relaxed);
    }
}


static void on_bind_render_targets_and_depth_stencil(
    reshade::api::command_list *,
    std::uint32_t count,
    const reshade::api::resource_view *rtvs,
    reshade::api::resource_view)
{
    const std::uint64_t handle =
        count != 0 && rtvs != nullptr
            ? rtvs[0].handle
            : 0;

    g_current_rtv_handle.store(
        handle,
        std::memory_order_relaxed);
}


static void on_reshade_present(
    reshade::api::effect_runtime *runtime)
{
    if (runtime != g_runtime.load(std::memory_order_relaxed))
        return;

    g_effects_rendered_this_frame.store(
        false,
        std::memory_order_relaxed);

    ensure_early_effects_target(runtime);
}


static void on_reshade_begin_effects(
    reshade::api::effect_runtime *runtime,
    reshade::api::command_list *,
    reshade::api::resource_view,
    reshade::api::resource_view)
{
    std::uint32_t runtime_width = 0;
    std::uint32_t runtime_height = 0;

    runtime->get_screenshot_width_and_height(
        &runtime_width,
        &runtime_height);

    g_output_width.store(runtime_width, std::memory_order_relaxed);
    g_output_height.store(runtime_height, std::memory_order_relaxed);

    update_depth_binding(
        runtime,
        runtime_width,
        runtime_height);

}


static const char *safe_gl_string(GLenum name)
{
    const GLubyte *const value = glGetString(name);

    return value != nullptr
        ? reinterpret_cast<const char *>(value)
        : "Unavailable";
}


static void draw_diagnostic_status(
    bool ok,
    const char *label)
{
    ImGui::Text(
        "%s %s",
        ok ? "[OK]" : "[--]",
        label);
}


struct depth_configuration
{
    char upside_down[64] = {};
    char reversed[64] = {};
    char logarithmic[64] = {};
    char far_plane[64] = {};

    bool has_upside_down = false;
    bool has_reversed = false;
    bool has_logarithmic = false;
    bool has_far_plane = false;

    bool upside_down_ok = false;
    bool reversed_ok = false;
    bool logarithmic_ok = false;
    bool far_plane_ok = false;
};


static depth_configuration read_depth_configuration(
    reshade::api::effect_runtime *runtime)
{
    depth_configuration result = {};

    if (runtime == nullptr)
        return result;

    result.has_upside_down =
        runtime->get_preprocessor_definition(
            "RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN",
            result.upside_down);

    result.has_reversed =
        runtime->get_preprocessor_definition(
            "RESHADE_DEPTH_INPUT_IS_REVERSED",
            result.reversed);

    result.has_logarithmic =
        runtime->get_preprocessor_definition(
            "RESHADE_DEPTH_INPUT_IS_LOGARITHMIC",
            result.logarithmic);

    result.has_far_plane =
        runtime->get_preprocessor_definition(
            "RESHADE_DEPTH_LINEARIZATION_FAR_PLANE",
            result.far_plane);

    // ReShade.fxh provides defaults when these definitions are absent:
    //
    //   UPSIDE_DOWN = 0
    //   REVERSED = 1
    //   LOGARITHMIC = 0
    //   FAR_PLANE = 1000.0
    //
    // SSO therefore requires explicit overrides for upside-down and
    // reversed depth, while logarithmic and far-plane may safely use
    // ReShade's built-in defaults.

    result.upside_down_ok =
        result.has_upside_down &&
        std::strcmp(result.upside_down, "1") == 0;

    result.reversed_ok =
        result.has_reversed &&
        std::strcmp(result.reversed, "0") == 0;

    result.logarithmic_ok =
        !result.has_logarithmic ||
        std::strcmp(result.logarithmic, "0") == 0;

    if (!result.has_far_plane)
    {
        result.far_plane_ok = true;
    }
    else
    {
        char *end = nullptr;

        const double value =
            std::strtod(result.far_plane, &end);

        result.far_plane_ok =
            end != result.far_plane &&
            end != nullptr &&
            *end == '\0' &&
            value >= 999.999 &&
            value <= 1000.001;
    }

    return result;
}


static const char *definition_value(
    bool exists,
    const char *value)
{
    return exists ? value : "not set";
}


static void draw_settings_overlay(
    reshade::api::effect_runtime *runtime)
{
    const bool runtime_ok =
        runtime != nullptr &&
        runtime == g_runtime.load(std::memory_order_relaxed);

    const bool opengl_ok =
        runtime_ok &&
        runtime->get_device()->get_api() ==
            reshade::api::device_api::opengl;

    const GLuint scene_fbo =
        g_scene_fbo.load(std::memory_order_relaxed);

    const std::uint32_t scene_width =
        g_scene_width.load(std::memory_order_relaxed);

    const std::uint32_t scene_height =
        g_scene_height.load(std::memory_order_relaxed);

    const std::uint32_t output_width =
        g_output_width.load(std::memory_order_relaxed);

    const std::uint32_t output_height =
        g_output_height.load(std::memory_order_relaxed);

    const std::uint64_t depth_key =
        g_last_depth_key.load(std::memory_order_relaxed);

    const bool scene_ok =
        scene_fbo != 0 &&
        scene_width != 0 &&
        scene_height != 0;

    const bool depth_ok =
        depth_key != 0;

    const bool early_target_ok =
        g_early_effects_texture != 0 &&
        g_early_effects_rtv != 0 &&
        g_early_effects_rtv_srgb != 0;

    const bool ui_safe_ok =
        g_logged_early_effects.load(std::memory_order_relaxed);

    const depth_configuration depth_config =
        read_depth_configuration(runtime);

    const bool depth_config_ok =
        depth_config.upside_down_ok &&
        depth_config.reversed_ok &&
        depth_config.logarithmic_ok &&
        depth_config.far_plane_ok;

    const bool everything_ok =
        runtime_ok &&
        opengl_ok &&
        scene_ok &&
        depth_ok &&
        early_target_ok &&
        ui_safe_ok &&
        depth_config_ok;

    ImGui::Text(
        "SSO Depth %s",
        SSODEPTH_VERSION);

    ImGui::Separator();

    if (everything_ok)
    {
        ImGui::TextWrapped(
            "Everything looks good. SSO Depth has detected the "
            "3D scene and depth buffer, and UI-safe effect "
            "rendering has been confirmed.");
    }
    else if (!scene_ok)
    {
        ImGui::TextWrapped(
            "Waiting for Star Stable's 3D scene. If you are "
            "already fully loaded into the game world, one or "
            "more checks below may need attention.");
    }
    else
    {
        ImGui::TextWrapped(
            "One or more checks have not been confirmed yet.");
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Self-test");

    draw_diagnostic_status(
        runtime_ok,
        "ReShade runtime detected");

    draw_diagnostic_status(
        opengl_ok,
        "OpenGL renderer detected");

    draw_diagnostic_status(
        scene_ok,
        "Main 3D scene framebuffer detected");

    draw_diagnostic_status(
        depth_ok,
        "Native SSO depth bridge confirmed");

    draw_diagnostic_status(
        early_target_ok,
        "Early-effects render target ready");

    draw_diagnostic_status(
        ui_safe_ok,
        "UI-safe effect rendering confirmed");

    ImGui::Separator();

    ImGui::TextUnformatted("Depth configuration");

    draw_diagnostic_status(
        depth_config.upside_down_ok,
        "Upside down = 1");

    draw_diagnostic_status(
        depth_config.reversed_ok,
        "Reversed = 0");

    draw_diagnostic_status(
        depth_config.logarithmic_ok,
        "Logarithmic = 0");

    draw_diagnostic_status(
        depth_config.far_plane_ok,
        "Far plane = 1000.0");

    if (!depth_config_ok)
    {
        ImGui::TextWrapped(
            "One or more ReShade depth settings do not match "
            "the configuration expected by SSO Depth.");
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Technical details"))
    {
        ImGui::Text(
            "Output resolution: %u x %u",
            output_width,
            output_height);

        ImGui::Text(
            "Scene framebuffer: %u",
            static_cast<unsigned int>(scene_fbo));

        ImGui::Text(
            "Scene resolution: %u x %u",
            scene_width,
            scene_height);

        if (depth_key != 0)
        {
            const std::uint32_t depth_fbo =
                static_cast<std::uint32_t>(
                    depth_key >> 32);

            const std::uint32_t depth_texture =
                static_cast<std::uint32_t>(
                    depth_key & 0xFFFFFFFFull);

            ImGui::Text(
                "Depth framebuffer: %u",
                depth_fbo);

            ImGui::Text(
                "Depth texture: %u",
                depth_texture);
        }
        else
        {
            ImGui::TextUnformatted(
                "Depth framebuffer: not detected");

            ImGui::TextUnformatted(
                "Depth texture: not detected");
        }

        ImGui::Spacing();

        ImGui::TextUnformatted("Depth preprocessor values:");

        ImGui::Text(
            "Upside down: %s",
            definition_value(
                depth_config.has_upside_down,
                depth_config.upside_down));

        ImGui::Text(
            "Reversed: %s",
            definition_value(
                depth_config.has_reversed,
                depth_config.reversed));

        ImGui::Text(
            "Logarithmic: %s",
            definition_value(
                depth_config.has_logarithmic,
                depth_config.logarithmic));

        ImGui::Text(
            "Far plane: %s",
            definition_value(
                depth_config.has_far_plane,
                depth_config.far_plane));

        ImGui::Spacing();

        if (opengl_ok)
        {
            ImGui::Text(
                "OpenGL vendor: %s",
                safe_gl_string(GL_VENDOR));

            ImGui::Text(
                "OpenGL renderer: %s",
                safe_gl_string(GL_RENDERER));

            ImGui::Text(
                "OpenGL version: %s",
                safe_gl_string(GL_VERSION));
        }

        ImGui::Spacing();

        ImGui::TextWrapped(
            "Loaded add-on: %s",
            g_addon_path[0] != '\0'
                ? g_addon_path
                : "Unavailable");

        ImGui::TextWrapped(
            "Game executable: %s",
            g_executable_path[0] != '\0'
                ? g_executable_path
                : "Unavailable");

        ImGui::TreePop();
    }

    ImGui::Spacing();
    ImGui::Separator();

    static bool report_copied = false;

    if (ImGui::Button("Copy diagnostic report"))
    {
        const std::uint32_t depth_fbo =
            static_cast<std::uint32_t>(
                depth_key >> 32);

        const std::uint32_t depth_texture =
            static_cast<std::uint32_t>(
                depth_key & 0xFFFFFFFFull);

        const char *const gl_vendor =
            opengl_ok
                ? safe_gl_string(GL_VENDOR)
                : "Unavailable";

        const char *const gl_renderer =
            opengl_ok
                ? safe_gl_string(GL_RENDERER)
                : "Unavailable";

        const char *const gl_version =
            opengl_ok
                ? safe_gl_string(GL_VERSION)
                : "Unavailable";

        char report[8192] = {};

        std::snprintf(
            report,
            sizeof(report),
            "SSO Depth %s\n"
            "Status: %s\n"
            "\n"
            "Self-test\n"
            "ReShade runtime: %s\n"
            "OpenGL renderer: %s\n"
            "Main 3D scene framebuffer: %s\n"
            "Native SSO depth bridge: %s\n"
            "Early-effects render target: %s\n"
            "UI-safe effect rendering: %s\n"
            "\n"
            "Depth configuration\n"
            "Upside down: %s (%s)\n"
            "Reversed: %s (%s)\n"
            "Logarithmic: %s\n"
            "Far plane: %s\n"
            "\n"
            "Rendering details\n"
            "Output resolution: %u x %u\n"
            "Scene resolution: %u x %u\n"
            "Scene framebuffer: %u\n"
            "Depth framebuffer: %u\n"
            "Depth texture: %u\n"
            "\n"
            "OpenGL\n"
            "Vendor: %s\n"
            "Renderer: %s\n"
            "Version: %s\n"
            "\n"
            "Paths\n"
            "Loaded add-on: %s\n"
            "Game executable: %s\n",
            SSODEPTH_VERSION,
            everything_ok ? "OK" : "CHECK FAILED",
            runtime_ok ? "OK" : "NOT DETECTED",
            opengl_ok ? "OK" : "NOT DETECTED",
            scene_ok ? "OK" : "NOT DETECTED",
            depth_ok ? "OK" : "NOT CONFIRMED",
            early_target_ok ? "OK" : "NOT READY",
            ui_safe_ok ? "OK" : "NOT CONFIRMED",
            definition_value(
                depth_config.has_upside_down,
                depth_config.upside_down),
            depth_config.upside_down_ok ? "OK" : "EXPECTED 1",
            definition_value(
                depth_config.has_reversed,
                depth_config.reversed),
            depth_config.reversed_ok ? "OK" : "EXPECTED 0",
            depth_config.has_logarithmic
                ? (depth_config.logarithmic_ok
                    ? "0 (OK)"
                    : depth_config.logarithmic)
                : "0 (ReShade default, OK)",
            depth_config.has_far_plane
                ? (depth_config.far_plane_ok
                    ? "1000.0 (OK)"
                    : depth_config.far_plane)
                : "1000.0 (ReShade default, OK)",
            output_width,
            output_height,
            scene_width,
            scene_height,
            static_cast<unsigned int>(scene_fbo),
            depth_fbo,
            depth_texture,
            gl_vendor,
            gl_renderer,
            gl_version,
            g_addon_path[0] != '\0'
                ? g_addon_path
                : "Unavailable",
            g_executable_path[0] != '\0'
                ? g_executable_path
                : "Unavailable");

        ImGui::SetClipboardText(report);
        report_copied = true;
    }

    if (report_copied)
    {
        ImGui::SameLine();
        ImGui::TextUnformatted("Copied!");
    }

    ImGui::TextWrapped(
        "If something is not working, copy this report and "
        "include it when asking for help.");
}


BOOL APIENTRY DllMain(
    HMODULE module,
    DWORD reason,
    LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        GetModuleFileNameA(
            module,
            g_addon_path,
            static_cast<DWORD>(sizeof(g_addon_path)));

        GetModuleFileNameA(
            nullptr,
            g_executable_path,
            static_cast<DWORD>(sizeof(g_executable_path)));

        if (!reshade::register_addon(module))
            return FALSE;

        // Register state tracking first so its captured state is current
        // when SSODepth's callbacks execute.
        state_tracking::register_events();

        reshade::register_event<reshade::addon_event::init_effect_runtime>(
            &on_init_effect_runtime);

        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(
            &on_destroy_effect_runtime);

        reshade::register_event<
            reshade::addon_event::bind_render_targets_and_depth_stencil>(
            &on_bind_render_targets_and_depth_stencil);

        reshade::register_event<reshade::addon_event::reshade_present>(
            &on_reshade_present);

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

        reshade::register_overlay(
            nullptr,
            &draw_settings_overlay);

        reshade::log::message(
            reshade::log::level::info,
            "SSO Depth loaded.");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        state_tracking::unregister_events();

        // unregister_addon also removes this add-on's remaining callbacks.
        reshade::unregister_addon(module);
    }

    return TRUE;
}
