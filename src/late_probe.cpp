#include <windows.h>
#include <GL/gl.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <algorithm>

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
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif

extern "C" __declspec(dllexport) const char *NAME = "SSO Depth";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Experimental Star Stable Online depth workaround for Wine/OpenGL.";

using RegisterAddonFn =
    bool (*)(void *, std::uint32_t);

using UnregisterAddonFn =
    void (*)(void *);

using RegisterEventForAddonFn =
    void (*)(void *, std::uint32_t, void *);

using UnregisterEventForAddonFn =
    void (*)(void *, std::uint32_t, void *);

using LogMessageFn =
    void (*)(void *, int, const char *);

using BindFramebufferFn =
    void (APIENTRY *)(GLenum, GLuint);

using CheckFramebufferStatusFn =
    GLenum (APIENTRY *)(GLenum);

using GetFramebufferAttachmentParameterivFn =
    void (APIENTRY *)(GLenum, GLenum, GLenum, GLint *);

static HMODULE g_module = nullptr;
static LogMessageFn g_log = nullptr;

static GLuint g_scene_fbo = 0;
static std::uint64_t g_frame = 0;
static std::uint64_t g_draws = 0;

static constexpr std::uint32_t EVENT_DRAW = 52;
static constexpr std::uint32_t EVENT_DRAW_INDEXED = 53;
static constexpr std::uint32_t EVENT_PRESENT = 74;
static constexpr std::uint32_t RESHADE_API_VERSION = 20;

static void log_message(const char *format, ...)
{
    if (g_log == nullptr)
        return;

    char buffer[1200];

    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    g_log(g_module, 3, buffer);
}

static void detect_scene_fbo()
{
    GLint viewport[4] = {};
    GLint draw_fbo = 0;
    GLint depth_bits = 0;

    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
    glGetIntegerv(GL_DEPTH_BITS, &depth_bits);

    const bool depth_test =
        glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;

    /*
     * This excludes SSO's 1024x1024 shadow/auxiliary pass
     * while matching the real full-resolution world pass.
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
    }
}

static bool on_draw(
    void *,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t)
{
    ++g_draws;
    detect_scene_fbo();
    return false;
}

static bool on_draw_indexed(
    void *,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t,
    std::int32_t,
    std::uint32_t)
{
    ++g_draws;
    detect_scene_fbo();
    return false;
}

static void late_probe()
{
    if (g_scene_fbo == 0)
        return;

    auto bind_framebuffer =
        reinterpret_cast<BindFramebufferFn>(
            wglGetProcAddress("glBindFramebuffer"));

    auto check_framebuffer =
        reinterpret_cast<CheckFramebufferStatusFn>(
            wglGetProcAddress("glCheckFramebufferStatus"));

    auto get_attachment =
        reinterpret_cast<GetFramebufferAttachmentParameterivFn>(
            wglGetProcAddress(
                "glGetFramebufferAttachmentParameteriv"));

    if (
        bind_framebuffer == nullptr ||
        check_framebuffer == nullptr ||
        get_attachment == nullptr)
    {
        log_message("Required framebuffer functions unavailable.");
        return;
    }

    GLint previous_read_fbo = 0;
    GLint viewport[4] = {};

    glGetIntegerv(
        GL_READ_FRAMEBUFFER_BINDING,
        &previous_read_fbo);

    glGetIntegerv(
        GL_VIEWPORT,
        viewport);

    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        g_scene_fbo);

    const GLenum status =
        check_framebuffer(GL_READ_FRAMEBUFFER);

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

    const GLint xs[5] =
    {
        viewport[2] / 2,
        viewport[2] / 4,
        viewport[2] * 3 / 4,
        viewport[2] / 2,
        viewport[2] / 2
    };

    const GLint ys[5] =
    {
        viewport[3] / 2,
        viewport[3] / 2,
        viewport[3] / 2,
        viewport[3] / 4,
        viewport[3] * 3 / 4
    };

    float depth[5] =
    {
        -1, -1, -1, -1, -1
    };

    while (glGetError() != GL_NO_ERROR)
    {
    }

    for (int i = 0; i < 5; ++i)
    {
        glReadPixels(
            xs[i],
            ys[i],
            1,
            1,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
            &depth[i]);
    }

    const GLenum error = glGetError();

    log_message(
        "LATE PROBE | frame=%llu | sceneFBO=%u | "
        "status=0x%X | attachmentType=0x%X | "
        "attachmentName=%d | "
        "depth=[%.7f %.7f %.7f %.7f %.7f] | "
        "glError=0x%X",
        static_cast<unsigned long long>(g_frame),
        g_scene_fbo,
        status,
        object_type,
        object_name,
        depth[0],
        depth[1],
        depth[2],
        depth[3],
        depth[4],
        error);

    bind_framebuffer(
        GL_READ_FRAMEBUFFER,
        static_cast<GLuint>(previous_read_fbo));
}

static void on_present(
    void *,
    void *,
    const void *,
    const void *,
    std::uint32_t,
    const void *)
{
    ++g_frame;

    /*
     * Don't hammer glReadPixels every frame.
     */
    if (
        g_scene_fbo != 0 &&
        (g_frame <= 5 || g_frame % 120 == 0))
    {
        late_probe();
    }

    g_draws = 0;
}

extern "C" __declspec(dllexport)
bool AddonInit(
    HMODULE addon_module,
    HMODULE reshade_module)
{
    g_module = addon_module;

    auto register_addon =
        reinterpret_cast<RegisterAddonFn>(
            GetProcAddress(
                reshade_module,
                "ReShadeRegisterAddon"));

    auto register_event =
        reinterpret_cast<RegisterEventForAddonFn>(
            GetProcAddress(
                reshade_module,
                "ReShadeRegisterEventForAddon"));

    g_log =
        reinterpret_cast<LogMessageFn>(
            GetProcAddress(
                reshade_module,
                "ReShadeLogMessage"));

    if (
        register_addon == nullptr ||
        register_event == nullptr ||
        g_log == nullptr)
    {
        return false;
    }

    if (!register_addon(
        addon_module,
        RESHADE_API_VERSION))
    {
        return false;
    }

    register_event(
        addon_module,
        EVENT_DRAW,
        reinterpret_cast<void *>(&on_draw));

    register_event(
        addon_module,
        EVENT_DRAW_INDEXED,
        reinterpret_cast<void *>(&on_draw_indexed));

    register_event(
        addon_module,
        EVENT_PRESENT,
        reinterpret_cast<void *>(&on_present));

    log_message(
        "Late scene-depth probe loaded.");

    return true;
}

extern "C" __declspec(dllexport)
void AddonUninit(
    HMODULE addon_module,
    HMODULE reshade_module)
{
    auto unregister_event =
        reinterpret_cast<UnregisterEventForAddonFn>(
            GetProcAddress(
                reshade_module,
                "ReShadeUnregisterEventForAddon"));

    auto unregister_addon =
        reinterpret_cast<UnregisterAddonFn>(
            GetProcAddress(
                reshade_module,
                "ReShadeUnregisterAddon"));

    if (unregister_event != nullptr)
    {
        unregister_event(
            addon_module,
            EVENT_DRAW,
            reinterpret_cast<void *>(&on_draw));

        unregister_event(
            addon_module,
            EVENT_DRAW_INDEXED,
            reinterpret_cast<void *>(&on_draw_indexed));

        unregister_event(
            addon_module,
            EVENT_PRESENT,
            reinterpret_cast<void *>(&on_present));
    }

    if (unregister_addon != nullptr)
        unregister_addon(addon_module);

    g_log = nullptr;
    g_module = nullptr;
}
