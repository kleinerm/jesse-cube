/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Chia-I Wu <olv@lunarg.com>
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Ian Elliott <ian@LunarG.com>
 * Author: Ian Elliott <ianelliott@google.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Gwan-gyeong Mun <elongbug@gmail.com>
 * Author: Tony Barbour <tony@LunarG.com>
 * Author: Bill Hollings <bill.hollings@brenwill.com>
 * Author: Mario Kleiner
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#if !defined(WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#define _USE_MATH_DEFINES
#define NO_STDIO_REDIRECT
#endif

// MK:
#include "glew.h"
#include <GL/gl.h>
#include <GL/glu.h>

#if !defined(WIN32)
#include <GL/glx.h>

#if defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
typedef Bool ( * PFNGLXGETSYNCVALUESOMLPROC) (Display* dpy, GLXDrawable drawable, int64_t* ust, int64_t* msc, int64_t* sbc);
PFNGLXGETSYNCVALUESOMLPROC glXGetSyncValuesOML = NULL;
#endif

#if defined(VK_USE_PLATFORM_XLIB_KHR) || defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
#include <X11/Xutil.h>

#endif
#if defined (VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>
#endif
#endif

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#define APP_NAME_STR_LEN 80
#endif  // _WIN32

#if defined(VK_USE_PLATFORM_MIR_KHR)
#warning "Cube does not have code for Mir at this time"
#endif

#ifdef ANDROID
#include "vulkan_wrapper.h"
#else
#include <vulkan/vulkan.h>
#endif

#include <vulkan/vk_sdk_platform.h>
#include "linmath.h"

#include "gettime.h"
#include "inttypes.h"
#define MILLION 1000000L
#define BILLION 1000000000L

#define DEMO_TEXTURE_COUNT 1
#define APP_SHORT_NAME "cube"
#define APP_LONG_NAME "The Vulkan Cube Demo Program"

// Allow a maximum of two outstanding presentation operations.
#define FRAME_LAG 1

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#if defined(NDEBUG) && defined(__GNUC__)
#define U_ASSERT_ONLY __attribute__((unused))
#else
#define U_ASSERT_ONLY
#endif

#if defined(__GNUC__)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

#ifdef _WIN32
bool in_callback = false;
#define ERR_EXIT(err_msg, err_class)                                             \
    do {                                                                         \
        if (!demo->suppress_popups) MessageBox(NULL, err_msg, err_class, MB_OK); \
        exit(1);                                                                 \
    } while (0)
void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf(fmt, va);
    fflush(stdout);
    va_end(va);
}

#elif defined __ANDROID__
#include <android/log.h>
#define ERR_EXIT(err_msg, err_class)                                    \
    do {                                                                \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Cube", err_msg)); \
        exit(1);                                                        \
    } while (0)
#ifdef VARARGS_WORKS_ON_ANDROID
void DbgMsg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    __android_log_print(ANDROID_LOG_INFO, "Cube", fmt, va);
    va_end(va);
}
#else  // VARARGS_WORKS_ON_ANDROID
#define DbgMsg(fmt, ...)                                                           \
    do {                                                                           \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Cube", fmt, ##__VA_ARGS__)); \
    } while (0)
#endif  // VARARGS_WORKS_ON_ANDROID
#else
#define ERR_EXIT(err_msg, err_class) \
    do {                             \
        printf("%s\n", err_msg);     \
        fflush(stdout);              \
        exit(1);                     \
    } while (0)
void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    printf(fmt, va);
    fflush(stdout);
    va_end(va);
}
#endif

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                                                              \
    {                                                                                                         \
        demo->fp##entrypoint = (PFN_vk##entrypoint)vkGetInstanceProcAddr(inst, "vk" #entrypoint);             \
        if (demo->fp##entrypoint == NULL) {                                                                   \
            ERR_EXIT("vkGetInstanceProcAddr failed to find vk" #entrypoint, "vkGetInstanceProcAddr Failure"); \
        }                                                                                                     \
    }

static PFN_vkGetDeviceProcAddr g_gdpa = NULL;

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                                                                    \
    {                                                                                                            \
        if (!g_gdpa) g_gdpa = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(demo->inst, "vkGetDeviceProcAddr"); \
        demo->fp##entrypoint = (PFN_vk##entrypoint)g_gdpa(dev, "vk" #entrypoint);                                \
        if (demo->fp##entrypoint == NULL) {                                                                      \
            ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint, "vkGetDeviceProcAddr Failure");        \
        }                                                                                                        \
    }

/*
 * structure to track all objects related to a texture.
 */
struct texture_object {
    VkSampler sampler;

    VkImage image;
    VkImageLayout imageLayout;

    VkMemoryAllocateInfo mem_alloc;
    VkDeviceMemory mem;
    VkImageView view;
    int32_t tex_width, tex_height;
};

static char *tex_files[] = {"jesse.ppm"};

static int validation_error = 0;

struct vktexcube_vs_uniform {
    // Must start with MVP
    float mvp[4][4];
    float position[12 * 3][4];
    float attr[12 * 3][4];
};

//--------------------------------------------------------------------------------------
// Mesh and VertexFormat Data
//--------------------------------------------------------------------------------------
// clang-format off
static const float g_vertex_buffer_data[] = {
    -1.0f, 1.0f, 0.0f,  // +Z side
    -1.0f,-1.0f, 0.0f,
     1.0f, 1.0f, 0.0f,
    -1.0f,-1.0f, 0.0f,
     1.0f,-1.0f, 0.0f,
     1.0f, 1.0f, 0.0f,

    -1.0f,-1.0f,-1.0f,  // -X side
    -1.0f,-1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,

    -1.0f,-1.0f,-1.0f,  // -Z side
     1.0f, 1.0f,-1.0f,
     1.0f,-1.0f,-1.0f,
    -1.0f,-1.0f,-1.0f,
    -1.0f, 1.0f,-1.0f,
     1.0f, 1.0f,-1.0f,

    -1.0f,-1.0f,-1.0f,  // -Y side
     1.0f,-1.0f,-1.0f,
     1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f,-1.0f,
     1.0f,-1.0f, 1.0f,
    -1.0f,-1.0f, 1.0f,

    -1.0f, 1.0f,-1.0f,  // +Y side
    -1.0f, 1.0f, 1.0f,
     1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f,-1.0f,
     1.0f, 1.0f, 1.0f,
     1.0f, 1.0f,-1.0f,

     1.0f, 1.0f,-1.0f,  // +X side
     1.0f, 1.0f, 1.0f,
     1.0f,-1.0f, 1.0f,
     1.0f,-1.0f, 1.0f,
     1.0f,-1.0f,-1.0f,
     1.0f, 1.0f,-1.0f,
};

static const float g_uv_buffer_data[] = {
    0.0f, 0.0f,  // +Z side
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,

    0.0f, 1.0f,  // -X side
    1.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,

    1.0f, 1.0f,  // -Z side
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 0.0f,

    1.0f, 0.0f,  // -Y side
    1.0f, 1.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,

    1.0f, 0.0f,  // +Y side
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,

    1.0f, 0.0f,  // +X side
    0.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 1.0f,
    1.0f, 1.0f,
    1.0f, 0.0f,
};
// clang-format on

void dumpMatrix(const char *note, mat4x4 MVP) {
    int i;

    printf("%s: \n", note);
    for (i = 0; i < 4; i++) {
        printf("%f, %f, %f, %f\n", MVP[i][0], MVP[i][1], MVP[i][2], MVP[i][3]);
    }
    printf("\n");
    fflush(stdout);
}

void dumpVec4(const char *note, vec4 vector) {
    printf("%s: \n", note);
    printf("%f, %f, %f, %f\n", vector[0], vector[1], vector[2], vector[3]);
    printf("\n");
    fflush(stdout);
}

VKAPI_ATTR VkBool32 VKAPI_CALL BreakCallback(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject,
                                             size_t location, int32_t msgCode, const char *pLayerPrefix, const char *pMsg,
                                             void *pUserData) {
#ifndef WIN32
    raise(SIGTRAP);
#else
    DebugBreak();
#endif

    return false;
}

typedef struct {
    VkImage image;
    VkCommandBuffer cmd;
    VkCommandBuffer graphics_to_present_cmd;
    VkImageView view;
    VkBuffer uniform_buffer;
    VkDeviceMemory uniform_memory;
    VkFramebuffer framebuffer;
    VkDescriptorSet descriptor_set;
} SwapchainImageResources;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
typedef struct _ShareHandles {
    HANDLE memory;
    HANDLE glReady;
    HANDLE glComplete;
} ShareHandles;
#else
typedef struct _ShareHandles {
    int memory;
    int glReady;
    int glComplete;
} ShareHandles;
#endif

struct demo {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
#define APP_NAME_STR_LEN 80
    HINSTANCE connection;         // hInstance - Windows Instance
    char name[APP_NAME_STR_LEN];  // Name to put on the window/icon
    HWND window;                  // hWnd - window handle
    POINT minsize;                // minimum window size
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    Display *display;
    Window xlib_window;
    Atom xlib_wm_delete_window;
#elif defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
    Display *display;
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_window_t xcb_window;
    int visualID;
    GLXContext context;
    GLXFBConfig fb_config;
    GLXDrawable drawable;
    xcb_intern_atom_reply_t *atom_wm_delete_window;
    bool leasedAlready;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *window;
    struct wl_shell *shell;
    struct wl_shell_surface *shell_surface;
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    ANativeWindow *window;
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
    void *window;
#endif
    VkDisplayKHR vkdisplay;
    VkSurfaceKHR surface;
//    VkKmsDisplayInfoKEITHP display_info;
    bool prepared;
    bool use_staging_buffer;
    bool separate_present_queue;

    bool VK_KHR_incremental_present_enabled;

    bool VK_GOOGLE_display_timing_enabled;
    bool syncd_with_actual_presents;
    uint64_t refresh_duration_multiplier;
    uint64_t target_IPD;  // image present duration (inverse of frame rate)
    uint64_t prev_desired_present_time;
    uint32_t next_present_id;
    uint32_t last_early_id;  // 0 if no early images
    uint32_t last_late_id;   // 0 if no late images

    VkInstance inst;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family_index;
    uint32_t present_queue_family_index;
    VkSemaphore image_acquired_semaphores[FRAME_LAG];
    VkSemaphore draw_complete_semaphores[FRAME_LAG];
    VkSemaphore image_ownership_semaphores[FRAME_LAG];
    VkPhysicalDeviceProperties gpu_props;
    VkQueueFamilyProperties *queue_props;
    VkPhysicalDeviceMemoryProperties memory_properties;

    uint32_t enabled_extension_count;
    uint32_t enabled_layer_count;
    char *extension_names[64];
    char *enabled_layers[64];

    int width, height;
    VkFormat format;
    VkColorSpaceKHR color_space;

    PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR fpGetPhysicalDeviceSurfaceCapabilities2KHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormats2KHR fpGetPhysicalDeviceSurfaceFormats2KHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
    PFN_vkQueuePresentKHR fpQueuePresentKHR;
    PFN_vkGetRefreshCycleDurationGOOGLE fpGetRefreshCycleDurationGOOGLE;
    PFN_vkGetPastPresentationTimingGOOGLE fpGetPastPresentationTimingGOOGLE;
//    PFN_vkRegisterDisplayEventEXT fpRegisterDisplayEventEXT;
//    PFN_vkGetSwapchainCounterEXT fpGetSwapchainCounterEXT;
#if defined(WIN32)
    PFN_vkAcquireFullScreenExclusiveModeEXT fpAcquireFullScreenExclusiveModeEXT;
    PFN_vkGetMemoryWin32HandleKHR fpGetMemoryWin32HandleKHR;
#endif
    // MK OpenGL -> Vulkan interop stuff:
    PFN_vkGetMemoryFdKHR fpGetMemoryFdKHR;
    VkFormat interop_tex_format;
    VkBool32 interop_tiled_texture;
    VkBool32 interop_enabled;
    VkBool32 timestamping_enabled;
    VkBool32 use_blit;

    // MK HDR stuff:
    VkBool32 hdr_enabled;
    VkBool32 local_dimming_enabled;
    VkBool32 amddisplaynativehdrExtFound;
    VkHdrMetadataEXT nativeDisplayHdrMetadata;
    PFN_vkSetHdrMetadataEXT fpSetHdrMetadataEXT;
    PFN_vkSetLocalDimmingAMD fpSetLocalDimmingAMD;

    // MK Stuff on the OpenGL side:
    GLuint glReady;
    GLuint glComplete;
    GLuint color;
    GLuint srctexture;
    GLuint dstfbo; // Destination fbo to which Vulkan backing memory is attached.
    GLuint srcfbo; // Source fbo into which our simulated renderer renders.
    GLuint hdr_shader; // HDR post-processing shader for EOTF application etc.
    GLuint vao;
    GLuint program;
    GLuint mem;

    // MK stuff for test patterns and colors:
    int testpattern;    // Id of test pattern to show.
    float tx, ty;       // Translation in x and y from center.
    float rgb[3];       // R, G, B intensity in nits.

    // MK mode selection:
    uint32_t max_width, max_height;
    float min_hz;

    uint32_t swapchainImageCount;
    VkSwapchainKHR swapchain;
    SwapchainImageResources *swapchain_image_resources;
    VkPresentModeKHR presentMode;
    VkFence fences[FRAME_LAG];
    int frame_index;

    // MK Flip completion fence for timestamping:
    VkFence flipcompletefence;
    int32_t waitMsecs;

    // GPU/driver to select on multi-gpu / multi-driver setup:
    int32_t gpuindex;

    // RandR output to select:
    char output_name[128];

    VkCommandPool cmd_pool;
    VkCommandPool present_cmd_pool;

    struct {
        VkFormat format;

        VkImage image;
        VkMemoryAllocateInfo mem_alloc;
        VkDeviceMemory mem;
        VkImageView view;
    } depth;

    struct texture_object textures[DEMO_TEXTURE_COUNT];
    struct texture_object staging_texture;

    VkCommandBuffer cmd;  // Buffer for initialization commands
    VkPipelineLayout pipeline_layout;
    VkDescriptorSetLayout desc_layout;
    VkPipelineCache pipelineCache;
    VkRenderPass render_pass;
    VkPipeline pipeline;

    mat4x4 projection_matrix;
    mat4x4 view_matrix;
    mat4x4 model_matrix;

    float spin_angle;
    float spin_increment;
    bool pause;

    VkShaderModule vert_shader_module;
    VkShaderModule frag_shader_module;

    VkDescriptorPool desc_pool;

    bool quit;
    int32_t curFrame;
    int32_t frameCount;
    bool validate;
    bool validate_checks_disabled;
    bool use_break;
    bool suppress_popups;
    PFN_vkCreateDebugReportCallbackEXT CreateDebugReportCallback;
    PFN_vkDestroyDebugReportCallbackEXT DestroyDebugReportCallback;
    VkDebugReportCallbackEXT msg_callback;
    PFN_vkDebugReportMessageEXT DebugReportMessage;

    uint32_t current_buffer;
    uint32_t queue_family_count;

    ShareHandles interophandles;
};

VKAPI_ATTR VkBool32 VKAPI_CALL dbgFunc(VkFlags msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location,
                                       int32_t msgCode, const char *pLayerPrefix, const char *pMsg, void *pUserData) {
    // clang-format off
    char *message = (char *)malloc(strlen(pMsg) + 100);

    assert(message);

    if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        sprintf(message, "WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        sprintf(message, "PERFORMANCE WARNING: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        sprintf(message, "ERROR: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        sprintf(message, "DEBUG: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    } else {
        sprintf(message, "INFORMATION: [%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
        validation_error = 1;
    }

#ifdef _WIN32

    in_callback = true;
    struct demo *demo = (struct demo*) pUserData;
    if (!demo->suppress_popups)
        MessageBox(NULL, message, "Alert", MB_OK);
    in_callback = false;

#elif defined(ANDROID)

    if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        __android_log_print(ANDROID_LOG_INFO,  APP_SHORT_NAME, "%s", message);
    } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        __android_log_print(ANDROID_LOG_WARN,  APP_SHORT_NAME, "%s", message);
    } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        __android_log_print(ANDROID_LOG_WARN,  APP_SHORT_NAME, "%s", message);
    } else if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        __android_log_print(ANDROID_LOG_ERROR, APP_SHORT_NAME, "%s", message);
    } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        __android_log_print(ANDROID_LOG_DEBUG, APP_SHORT_NAME, "%s", message);
    } else {
        __android_log_print(ANDROID_LOG_INFO,  APP_SHORT_NAME, "%s", message);
    }

#else

    printf("%s\n", message);
    fflush(stdout);

#endif

    free(message);

    //clang-format on

    /*
    * false indicates that layer should not bail-out of an
    * API call that had validation failures. This may mean that the
    * app dies inside the driver due to invalid parameter(s).
    * That's what would happen without validation layers, so we'll
    * keep that behavior here.
    */
    return false;
}

bool ActualTimeLate(uint64_t desired, uint64_t actual, uint64_t rdur) {
    // The desired time was the earliest time that the present should have
    // occured.  In almost every case, the actual time should be later than the
    // desired time.  We should only consider the actual time "late" if it is
    // after "desired + rdur".
    if (actual <= desired) {
        // The actual time was before or equal to the desired time.  This will
        // probably never happen, but in case it does, return false since the
        // present was obviously NOT late.
        return false;
    }
    uint64_t deadline = actual + rdur;
    if (actual > deadline) {
        return true;
    } else {
        return false;
    }
}
bool CanPresentEarlier(uint64_t earliest,
                       uint64_t actual,
                       uint64_t margin,
                       uint64_t rdur) {
    if (earliest < actual) {
        // Consider whether this present could have occured earlier.  Make sure
        // that earliest time was at least 2msec earlier than actual time, and
        // that the margin was at least 2msec:
        uint64_t diff = actual - earliest;
        if ((diff >= (2 * MILLION)) && (margin >= (2 * MILLION))) {
            // This present could have occured earlier because both: 1) the
            // earliest time was at least 2 msec before actual time, and 2) the
            // margin was at least 2msec.
            return true;
        }
    }
    return false;
}

// Forward declaration:
static void demo_resize(struct demo *demo);

static bool memory_type_from_properties(struct demo *demo, uint32_t typeBits,
                                        VkFlags requirements_mask,
                                        uint32_t *typeIndex) {
    // Search memtypes to find first index with those properties
    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++) {
        if ((typeBits & 1) == 1) {
            // Type is available, does it match user properties?
            if ((demo->memory_properties.memoryTypes[i].propertyFlags &
                 requirements_mask) == requirements_mask) {
                *typeIndex = i;
                return true;
            }
        }
        typeBits >>= 1;
    }
    // No memory types matched, return failure
    return false;
}

static void demo_flush_init_cmd(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;

    // This function could get called twice if the texture uses a staging buffer
    // In that case the second call should be ignored
    if (demo->cmd == VK_NULL_HANDLE)
        return;

    err = vkEndCommandBuffer(demo->cmd);
    assert(!err);

    VkFence fence;
    VkFenceCreateInfo fence_ci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                  .pNext = NULL,
                                  .flags = 0};
    err = vkCreateFence(demo->device, &fence_ci, NULL, &fence);
    assert(!err);

    const VkCommandBuffer cmd_bufs[] = {demo->cmd};
    VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = NULL,
                                .waitSemaphoreCount = 0,
                                .pWaitSemaphores = NULL,
                                .pWaitDstStageMask = NULL,
                                .commandBufferCount = 1,
                                .pCommandBuffers = cmd_bufs,
                                .signalSemaphoreCount = 0,
                                .pSignalSemaphores = NULL};

    err = vkQueueSubmit(demo->graphics_queue, 1, &submit_info, fence);
    assert(!err);

    err = vkWaitForFences(demo->device, 1, &fence, VK_TRUE, UINT64_MAX);
    assert(!err);

    vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1, cmd_bufs);
    vkDestroyFence(demo->device, fence, NULL);
    demo->cmd = VK_NULL_HANDLE;
}

static void demo_set_image_layout(struct demo *demo, VkImage image,
                                  VkImageAspectFlags aspectMask,
                                  VkImageLayout old_image_layout,
                                  VkImageLayout new_image_layout,
                                  VkAccessFlagBits srcAccessMask,
                                  VkPipelineStageFlags src_stages,
                                  VkPipelineStageFlags dest_stages,
                                  VkCommandBuffer cmd_buf) {
    if (!cmd_buf)
        assert(demo->cmd);

    VkImageMemoryBarrier image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .srcAccessMask = srcAccessMask,
        .dstAccessMask = 0,
        .oldLayout = old_image_layout,
        .newLayout = new_image_layout,
        .image = image,
        .subresourceRange = {aspectMask, 0, 1, 0, 1}};

    switch (new_image_layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        /* Make sure anything that was copying from this image has completed */
        image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        image_memory_barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        image_memory_barrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        break;

    default:
        image_memory_barrier.dstAccessMask = 0;
        break;
    }


    VkImageMemoryBarrier *pmemory_barrier = &image_memory_barrier;

    vkCmdPipelineBarrier((cmd_buf) ? cmd_buf : demo->cmd, src_stages, dest_stages, 0, 0, NULL, 0,
                         NULL, 1, pmemory_barrier);
}

static void demo_draw_build_cmd(struct demo *demo, VkCommandBuffer cmd_buf) {
    if (demo->use_blit) {
        VkResult U_ASSERT_ONLY err;

        const VkCommandBufferBeginInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
            .pInheritanceInfo = NULL,
        };

        err = vkBeginCommandBuffer(cmd_buf, &cmd_buf_info);
        assert(!err);

        demo_set_image_layout(demo, demo->textures[0].image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              demo->textures[0].imageLayout,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              cmd_buf);

        demo_set_image_layout(demo, demo->swapchain_image_resources[demo->current_buffer].image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_ACCESS_MEMORY_READ_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              cmd_buf);

        // Do pixel format of interop texture and swapchain image match?
        if (demo->interop_tex_format != demo->format) {
            // No: Need pixel color format conversion -> blit image:
            VkImageBlit blit_region = {
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .srcOffsets = {{0, 0, 0}, {demo->width, demo->height, 1}},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffsets = {{0, 0, 0}, {demo->width, demo->height, 1}}
            };

            vkCmdBlitImage(
                cmd_buf, demo->textures[0].image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, demo->swapchain_image_resources[demo->current_buffer].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_region, VK_FILTER_NEAREST);

            printf("Swapchainbuffer %d: Using vkCmdBlitImage() blit for interop -> swapchain transfer.\n", demo->current_buffer);
        }
        else {
            // Yes: Can do a memcpy() style copy image:
            VkImageCopy copy_region = {
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .srcOffset = {0, 0, 0},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffset = {0, 0, 0},
                .extent = {demo->width, demo->height, 1},
            };

            vkCmdCopyImage(
                cmd_buf, demo->textures[0].image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, demo->swapchain_image_resources[demo->current_buffer].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

            printf("Swapchainbuffer %d: Using vkCmdCopyImage() copy for interop -> swapchain transfer.\n", demo->current_buffer);
        }

        demo_set_image_layout(demo, demo->textures[0].image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              demo->textures[0].imageLayout,
                              VK_ACCESS_TRANSFER_READ_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              cmd_buf);

        demo_set_image_layout(demo, demo->swapchain_image_resources[demo->current_buffer].image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              cmd_buf);

        err = vkEndCommandBuffer(cmd_buf);
        assert(!err);
    }
    else {
        const VkCommandBufferBeginInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
            .pInheritanceInfo = NULL,
        };
        const VkClearValue clear_values[2] = {
                //[0] = {.color.float32 = {0.05f, 0.05f, 0.05f, 0.0f}},
                [0] = {.color.float32 = {0.0f, 0.0f, 0.0f, 0.0f}},
                [1] = {.depthStencil = {1.0f, 0}},
        };
        const VkRenderPassBeginInfo rp_begin = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = NULL,
            .renderPass = demo->render_pass,
            .framebuffer = demo->swapchain_image_resources[demo->current_buffer].framebuffer,
            .renderArea.offset.x = 0,
            .renderArea.offset.y = 0,
            .renderArea.extent.width = demo->width,
            .renderArea.extent.height = demo->height,
            .clearValueCount = 2,
            .pClearValues = clear_values,
        };
        VkResult U_ASSERT_ONLY err;

        err = vkBeginCommandBuffer(cmd_buf, &cmd_buf_info);
        assert(!err);
        vkCmdBeginRenderPass(cmd_buf, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, demo->pipeline);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                demo->pipeline_layout, 0, 1,
                                &demo->swapchain_image_resources[demo->current_buffer].descriptor_set,
                                0, NULL);
        VkViewport viewport;
        memset(&viewport, 0, sizeof(viewport));
        viewport.height = (float)demo->height;
        viewport.width = (float)demo->width;
        viewport.minDepth = (float)0.0f;
        viewport.maxDepth = (float)1.0f;
        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

        VkRect2D scissor;
        memset(&scissor, 0, sizeof(scissor));
        scissor.extent.width = demo->width;
        scissor.extent.height = demo->height;
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);
        vkCmdDraw(cmd_buf, 2 * 3, 1, 0, 0);
        // Note that ending the renderpass changes the image's layout from
        // COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
        vkCmdEndRenderPass(cmd_buf);

        if (demo->separate_present_queue) {
            printf("Swapchainbuffer %d: Need separate_present_queue!!!\n", demo->current_buffer);

            // We have to transfer ownership from the graphics queue family to the
            // present queue family to be able to present.  Note that we don't have
            // to transfer from present queue family back to graphics queue family at
            // the start of the next frame because we don't care about the image's
            // contents at that point.
            VkImageMemoryBarrier image_ownership_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = NULL,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = demo->graphics_queue_family_index,
                .dstQueueFamilyIndex = demo->present_queue_family_index,
                .image = demo->swapchain_image_resources[demo->current_buffer].image,
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

            vkCmdPipelineBarrier(cmd_buf,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                                0, NULL, 0, NULL, 1, &image_ownership_barrier);
        }
        err = vkEndCommandBuffer(cmd_buf);
        assert(!err);

        printf("Swapchainbuffer %d: Using 3D quad rendering + passthrough shader for interop -> swapchain transfer.\n", demo->current_buffer);
    }
}

void demo_build_image_ownership_cmd(struct demo *demo, int i) {
    VkResult U_ASSERT_ONLY err;

    const VkCommandBufferBeginInfo cmd_buf_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        .pInheritanceInfo = NULL,
    };
    err = vkBeginCommandBuffer(demo->swapchain_image_resources[i].graphics_to_present_cmd,
                               &cmd_buf_info);
    assert(!err);

    VkImageMemoryBarrier image_ownership_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = demo->graphics_queue_family_index,
        .dstQueueFamilyIndex = demo->present_queue_family_index,
        .image = demo->swapchain_image_resources[i].image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    vkCmdPipelineBarrier(demo->swapchain_image_resources[i].graphics_to_present_cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         NULL, 0, NULL, 1, &image_ownership_barrier);
    err = vkEndCommandBuffer(demo->swapchain_image_resources[i].graphics_to_present_cmd);
    assert(!err);
}

void demo_update_data_buffer(struct demo *demo) {
    mat4x4 MVP, Model, VP;
    int matrixSize = sizeof(MVP);
    uint8_t *pData;
    VkResult U_ASSERT_ONLY err;
    float spin_angle;

    mat4x4_mul(VP, demo->projection_matrix, demo->view_matrix);

/*
    // Rotate around the Y axis
    mat4x4_dup(Model, demo->model_matrix);

    spin_angle = demo->spin_angle;

    // Make the cube spin at a constant rate
    if (demo->VK_GOOGLE_display_timing_enabled)
        spin_angle *= (float) demo->target_IPD / (1.0f / 30.0f * 1e9);

    mat4x4_rotate(demo->model_matrix, Model, 0.0f, 1.0f, 0.0f,
                  (float)degreesToRadians(spin_angle));
    mat4x4_mul(MVP, VP, demo->model_matrix);
*/

    err = vkMapMemory(demo->device,
                      demo->swapchain_image_resources[demo->current_buffer].uniform_memory, 0,
                      VK_WHOLE_SIZE, 0, (void **)&pData);
    assert(!err);

    //memcpy(pData, (const void *)&MVP[0][0], matrixSize);
    memcpy(pData, (const void *)&VP[0][0], matrixSize);

    vkUnmapMemory(demo->device, demo->swapchain_image_resources[demo->current_buffer].uniform_memory);
}

static uint64_t
DemoRefreshDuration(struct demo *demo) {
   VkRefreshCycleDurationGOOGLE rc_dur;
   VkResult err;
   err = demo->fpGetRefreshCycleDurationGOOGLE(demo->device,
                           demo->swapchain,
                           &rc_dur);
   assert(!err);
   return rc_dur.refreshDuration;
}

void DemoUpdateTargetIPD(struct demo *demo) {
    // Look at what happened to previous presents, and make appropriate
    // adjustments in timing:
    VkResult U_ASSERT_ONLY err;
    VkPastPresentationTimingGOOGLE* past = NULL;
    uint32_t count = 0;

    err = demo->fpGetPastPresentationTimingGOOGLE(demo->device,
                                                  demo->swapchain,
                                                  &count,
                                                  NULL);
    assert(!err);
    if (count) {
        past = (VkPastPresentationTimingGOOGLE*) malloc(sizeof(VkPastPresentationTimingGOOGLE) * count);
        assert(past);
        err = demo->fpGetPastPresentationTimingGOOGLE(demo->device,
                                                      demo->swapchain,
                                                      &count,
                                                      past);
        assert(!err);

    uint64_t refresh_duration = DemoRefreshDuration(demo);

        bool early = false;
        bool late = false;
        bool calibrate_next = false;
        for (uint32_t i = 0 ; i < count ; i++) {

            printf("%d: desired %f actual %f earliest %f gap %f\n",
                   i,
                   past[i].desiredPresentTime / 1e9,
                   past[i].actualPresentTime / 1e9,
                   past[i].earliestPresentTime / 1e9,
                   past[i].actualPresentTime / 1e9 - past[i].earliestPresentTime / 1e9);


            if (!demo->syncd_with_actual_presents) {
                // This is the first time that we've received an
                // actualPresentTime for this swapchain.  In order to not
                // perceive these early frames as "late", we need to sync-up
                // our future desiredPresentTime's with the
                // actualPresentTime(s) that we're receiving now.
                calibrate_next = true;

                // So that we don't suspect any pending presents as late,
                // record them all as suspected-late presents:
                demo->last_late_id = demo->next_present_id - 1;
                demo->last_early_id = 0;
                demo->syncd_with_actual_presents = true;
                break;
            } else if (CanPresentEarlier(past[i].earliestPresentTime,
                                         past[i].actualPresentTime,
                                         past[i].presentMargin,
                                         refresh_duration)) {
                // This image could have been presented earlier.  We don't want
                // to decrease the target_IPD until we've seen early presents
                // for at least two seconds.
                if (demo->last_early_id == past[i].presentID) {
                    // We've now seen two seconds worth of early presents.
                    // Flag it as such, and reset the counter:
                    early = true;
                    demo->last_early_id = 0;
                } else if (demo->last_early_id == 0) {
                    // This is the first early present we've seen.
                    // Calculate the presentID for two seconds from now.
                    uint64_t lastEarlyTime =
                        past[i].actualPresentTime + (2 * BILLION);
                    uint32_t howManyPresents =
                        (uint32_t)((lastEarlyTime - past[i].actualPresentTime) / demo->target_IPD);
                    demo->last_early_id = past[i].presentID + howManyPresents;
                } else {
                    // We are in the midst of a set of early images,
                    // and so we won't do anything.
                }
                late = false;
                demo->last_late_id = 0;
            } else if (ActualTimeLate(past[i].desiredPresentTime,
                                      past[i].actualPresentTime,
                                      refresh_duration)) {
                // This image was presented after its desired time.  Since
                // there's a delay between calling vkQueuePresentKHR and when
                // we get the timing data, several presents may have been late.
                // Thus, we need to threat all of the outstanding presents as
                // being likely late, so that we only increase the target_IPD
                // once for all of those presents.
                if ((demo->last_late_id == 0) ||
                    (demo->last_late_id < past[i].presentID)) {
                    late = true;
                    // Record the last suspected-late present:
                    demo->last_late_id = demo->next_present_id - 1;
                } else {
                    // We are in the midst of a set of likely-late images,
                    // and so we won't do anything.
                }
                early = false;
                demo->last_early_id = 0;
            } else {
                // Since this image was not presented early or late, reset
                // any sets of early or late presentIDs:
                early = false;
                late = false;
                calibrate_next = true;
                demo->last_early_id = 0;
                demo->last_late_id = 0;
            }
        }

        if (early) {
            // Since we've seen at least two-seconds worth of presnts that
            // could have occured earlier than desired, let's decrease the
            // target_IPD (i.e. increase the frame rate):
            //
            // TODO(ianelliott): Try to calculate a better target_IPD based
            // on the most recently-seen present (this is overly-simplistic).
            demo->refresh_duration_multiplier--;
            if (demo->refresh_duration_multiplier == 0) {
                // This should never happen, but in case it does, don't
                // try to go faster.
                demo->refresh_duration_multiplier = 1;
            }
        }
        if (late) {
            // Since we found a new instance of a late present, we want to
            // increase the target_IPD (i.e. decrease the frame rate):
            //
            // TODO(ianelliott): Try to calculate a better target_IPD based
            // on the most recently-seen present (this is overly-simplistic).
            demo->refresh_duration_multiplier++;
        }
        demo->target_IPD =
        refresh_duration * demo->refresh_duration_multiplier;

        if (calibrate_next) {
            int64_t multiple = demo->next_present_id - past[count-1].presentID - 1;
            demo->prev_desired_present_time =
                (past[count-1].actualPresentTime +
                 (multiple * demo->target_IPD));
        }
    }
}

void setHdrMetadata(struct demo *demo, float maxL, float avgL) {
    VkHdrMetadataEXT hdr_metadata;

    if (!demo->hdr_enabled)
        return;

    memset(&hdr_metadata, 0, sizeof(hdr_metadata));

    hdr_metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    hdr_metadata.pNext = NULL;

    // Note: These are the mastering display properties of
    // my Samsung CH27HG70 FreeSync2 HDR monitor.
    VkXYColorEXT pr = { 0.6767, 0.3164 };
    VkXYColorEXT pg = { 0.2753, 0.6611 };
    VkXYColorEXT pb = { 0.1523, 0.0615 };
    VkXYColorEXT wp = { 0.3134, 0.3291 };

    // BT 2020 color space selected as output color space?
    if (false &&
        (demo->color_space == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
        demo->color_space == VK_COLOR_SPACE_HDR10_HLG_EXT ||
        demo->color_space == VK_COLOR_SPACE_BT2020_LINEAR_EXT)) {
        printf("HDR metadata for BT2020 colorspace assigned.\n");
        // Override to its gamut:
        pr.x = 0.708;
        pr.y = 0.292;
        pg.x = 0.170;
        pg.y = 0.797;
        pb.x = 0.131;
        pb.y = 0.046;

        // D65 white point:
        wp.x = 0.3127;
        wp.y = 0.3290;
    }

    if (true) {
        // Use  display native color gamut and properties:
        printf("HDR metadata for display native gamut and luminance levels assigned.\n");
        memcpy(&hdr_metadata, &demo->nativeDisplayHdrMetadata, sizeof(hdr_metadata));

        // Hack for AMD Vulkan driver on Windows-10: Does set bogus/wrong maxLuminance,
        // specifically it wrongly sets maxLuminance = maxFrameAverageLightLevel, and
        // maxFrameAverageLightLevel == 0. If we detect this, override with known values
        // from Samsung CH27HG70 for the moment:
        if (hdr_metadata.maxLuminance < 400) {
            printf("Driver bug: maxLuminance wrong -- Setting to hard-coded 603.666 nits.\n");
            hdr_metadata.maxLuminance = 603.666;
        }

        // If maxCLL is undefined, set it to max mastering display luminance:
        if (hdr_metadata.maxContentLightLevel == 0)
            hdr_metadata.maxContentLightLevel = hdr_metadata.maxLuminance;

        // If maxFall is missing, set it to half maxCLL as a reasonable setting:
        if (hdr_metadata.maxFrameAverageLightLevel == 0) {
            printf("Driver bug? maxFrameAverageLightLevel missing -- Setting to half maxContentLightLevel.\n");
            hdr_metadata.maxFrameAverageLightLevel = hdr_metadata.maxContentLightLevel / 2;
        }
    }

    // Use maxL for maximum light levels, if provided:
    if (maxL > 0) {
        hdr_metadata.maxLuminance = maxL;
        hdr_metadata.maxContentLightLevel = maxL;
    }

    // Use avgL for maxFALL, if provided:
    if (avgL > 0) {
        hdr_metadata.maxFrameAverageLightLevel = avgL;
    }

    // Minimum luminance is zero:
    hdr_metadata.minLuminance = 0.0;

    printf("Set HDR DATA to:\n");
    printf("Display Gamut  R: [%f, %f]\n", hdr_metadata.displayPrimaryRed.x, hdr_metadata.displayPrimaryRed.y);
    printf("Display Gamut  G: [%f, %f]\n", hdr_metadata.displayPrimaryGreen.x, hdr_metadata.displayPrimaryGreen.y);
    printf("Display Gamut  B: [%f, %f]\n", hdr_metadata.displayPrimaryBlue.x, hdr_metadata.displayPrimaryBlue.y);
    printf("Display Gamut WP: [%f, %f]\n", hdr_metadata.whitePoint.x, hdr_metadata.whitePoint.y);
    printf("Display minLuminance: %f nits\n", hdr_metadata.minLuminance);
    printf("Display maxLuminance: %f nits\n", hdr_metadata.maxLuminance);
    printf("Content maxFrameAverageLightLevel: %f nits\n", hdr_metadata.maxFrameAverageLightLevel);
    printf("Content maxContentLightLevel: %f nits\n", hdr_metadata.maxContentLightLevel);

    demo->fpSetHdrMetadataEXT(demo->device, 1, &demo->swapchain, &hdr_metadata);
}

// Forward define:
void draw_opengl(struct demo *demo);

static void demo_draw(struct demo *demo) {
    static uint64_t tStartTime = 0;
    static uint64_t tlastSwapComplete = 0;
    static uint64_t tPostSwapRequested = 0;
    static bool firsttime = true;
    uint64_t tSwapComplete;
    VkResult U_ASSERT_ONLY err;

    if (tlastSwapComplete == 0) {
        tlastSwapComplete = getTimeInNanoseconds();
        tStartTime = tlastSwapComplete;
    }

    if (false) {
        // Ensure no more than FRAME_LAG renderings are outstanding
        vkWaitForFences(demo->device, 1, &demo->fences[demo->frame_index], VK_TRUE, UINT64_MAX);
        vkResetFences(demo->device, 1, &demo->fences[demo->frame_index]);
    }

    // Get the index of the next available swapchain image:
    // Both image_acquired_semaphores[demo->frame_index] and the flipcompletefence
    // will signal when the display engine is done with scanning out the acquired
    // image, ergo, when it was replaced as old frontbuffer by a new frontbuffer,
    // which was our old backbuffer, iow. when the previously scheduled swap/present
    // actually completed due to kms-pageflip completion:
    err = demo->fpAcquireNextImageKHR(demo->device, demo->swapchain, UINT64_MAX,
                                      demo->image_acquired_semaphores[demo->frame_index],
                                      demo->flipcompletefence, &demo->current_buffer);

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        // demo->swapchain is out of date (e.g. the window was resized) and
        // must be recreated:
        demo->frame_index += 1;
        demo->frame_index %= FRAME_LAG;

        demo_resize(demo);
        demo_draw(demo);
        return;
    } else if (err == VK_SUBOPTIMAL_KHR) {
        // demo->swapchain is not as optimal as it could be, but the platform's
        // presentation engine will still present the image correctly.
    } else {
        assert(!err);
    }

    // Wait for flipcompletefence to signal, iow. for confirmed flip completion aka
    // visual stimulus onset. Then reset the fence and timestamp the moment:
    vkWaitForFences(demo->device, 1, &demo->flipcompletefence, VK_TRUE, UINT64_MAX);
    vkResetFences(demo->device, 1, &demo->flipcompletefence);
    tSwapComplete = getTimeInNanoseconds();

#if defined(VK_USE_PLATFORM_XLIB_XRANDR_EXT)
    // Use the precise timestamping, based on high-precision vblank timestamps iff we present synchronized
    // to vblank for tear-free presentation:
    if (demo->presentMode == VK_PRESENT_MODE_FIFO_KHR || demo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
        // Under Linux + X11 we can (ab)use our X11 window on the same output that is
        // leased out to Vulkan to use the glXGetSyncValuesOML() call on the Mesa FOSS
        // based drivers to get a precise start of scanout timestamp at end of most
        // recent vblank. Ideally this will be almost identical to tSwapComplete, ie.
        // tSwapComplete is a noisy approximation of the proper queried ust here.
        // Indeed, this works on AMD:
        uint64_t ust, msc, sbc;
        double serror;

        if ((NULL != glXGetSyncValuesOML) && glXGetSyncValuesOML(demo->display, demo->drawable, &ust, &msc, &sbc)) {
            // Timestamp disagreement of less than 1 msec is considered correct:
            serror = ((double) tSwapComplete / 1000.0) - (double) ust;
            if (demo->timestamping_enabled) {
                if (serror < 1000)
                    printf("OK: ");

                printf("msc %li, tSwapComplete %li - ust %li = %f usecs stimonset error: ", msc, tSwapComplete, ust * 1000, serror);
            }

            // Override with accurate value:
            tSwapComplete = ust * 1000;
        }
    }
#endif

    if (demo->timestamping_enabled)
        printf("ifi = %f msecs. tSwapComplete - tPostSwapRequested = %f msecs.\n",
               (double)(tSwapComplete - tlastSwapComplete) / 1000000.0,
               (double)(tSwapComplete - tPostSwapRequested) / 1000000.0);

    // Update last swap complete for next cycle:
    tlastSwapComplete = tSwapComplete;

    if (demo->VK_GOOGLE_display_timing_enabled) {
        // Look at what happened to previous presents, and make appropriate
        // adjustments in timing:
        DemoUpdateTargetIPD(demo);

        // Note: a real application would position its geometry to that it's in
        // the correct locatoin for when the next image is presented.  It might
        // also wait, so that there's less latency between any input and when
        // the next image is rendered/presented.  This demo program is so
        // simple that it doesn't do either of those.
    }

    #if defined(VK_USE_PLATFORM_DISPLAY_KHR) || defined(VK_USE_PLATFORM_WIN32_KHR)
        draw_opengl(demo);
    #endif

    demo_update_data_buffer(demo);

    // Wait for the image acquired semaphore to be signaled to ensure
    // that the image won't be rendered to until the presentation
    // engine has fully released ownership to the application, and it is
    // okay to render to the image.
    VkPipelineStageFlags pipe_stage_flags;
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.pWaitDstStageMask = &pipe_stage_flags;
    pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &demo->image_acquired_semaphores[demo->frame_index];
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &demo->swapchain_image_resources[demo->current_buffer].cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &demo->draw_complete_semaphores[demo->frame_index];
    err = vkQueueSubmit(demo->graphics_queue, 1, &submit_info,
                        demo->fences[demo->frame_index]);
    assert(!err);

    if (demo->separate_present_queue) {
        // If we are using separate queues, change image ownership to the
        // present queue before presenting, waiting for the draw complete
        // semaphore and signalling the ownership released semaphore when finished
        VkFence nullFence = VK_NULL_HANDLE;
        pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &demo->draw_complete_semaphores[demo->frame_index];
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers =
            &demo->swapchain_image_resources[demo->current_buffer].graphics_to_present_cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &demo->image_ownership_semaphores[demo->frame_index];
        err = vkQueueSubmit(demo->present_queue, 1, &submit_info, nullFence);
        assert(!err);
    }

    // If we are using separate queues we have to wait for image ownership,
    // otherwise wait for draw complete
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = (demo->separate_present_queue)
                               ? &demo->image_ownership_semaphores[demo->frame_index]
                               : &demo->draw_complete_semaphores[demo->frame_index],
        .swapchainCount = 1,
        .pSwapchains = &demo->swapchain,
        .pImageIndices = &demo->current_buffer,
    };

    if (demo->VK_KHR_incremental_present_enabled) {
        // If using VK_KHR_incremental_present, we provide a hint of the region
        // that contains changed content relative to the previously-presented
        // image.  The implementation can use this hint in order to save
        // work/power (by only copying the region in the hint).  The
        // implementation is free to ignore the hint though, and so we must
        // ensure that the entire image has the correctly-drawn content.
        uint32_t eighthOfWidth = demo->width / 8;
        uint32_t eighthOfHeight = demo->height / 8;
        VkRectLayerKHR rect = {
            .offset.x = eighthOfWidth,
            .offset.y = eighthOfHeight,
            .extent.width = eighthOfWidth * 6,
            .extent.height = eighthOfHeight * 6,
            .layer = 0,
        };
        VkPresentRegionKHR region = {
            .rectangleCount = 1,
            .pRectangles = &rect,
        };
        VkPresentRegionsKHR regions = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
            .pNext = present.pNext,
            .swapchainCount = present.swapchainCount,
            .pRegions = &region,
        };
        present.pNext = &regions;
    }

    if (demo->VK_GOOGLE_display_timing_enabled) {
        VkPresentTimeGOOGLE ptime;
        if (demo->prev_desired_present_time == 0) {
            // This must be the first present for this swapchain.
            //
            // We don't know where we are relative to the presentation engine's
            // display's refresh cycle.  We also don't know how long rendering
            // takes.  Let's make a grossly-simplified assumption that the
            // desiredPresentTime should be half way between now and
            // now+target_IPD.  We will adjust over time.
            uint64_t curtime = getTimeInNanoseconds();
            if (curtime == 0) {
                // Since we didn't find out the current time, don't give a
                // desiredPresentTime:
                ptime.desiredPresentTime = 0;
            } else {
                ptime.desiredPresentTime = curtime + (demo->target_IPD >> 1);
            }
        } else {
            ptime.desiredPresentTime = (demo->prev_desired_present_time +
                                        demo->target_IPD);
        }

        printf("\tdesired present time %f delta %f\n",
               ptime.desiredPresentTime / 1e9,
               ptime.desiredPresentTime / 1e9 - demo->prev_desired_present_time / 1e9);

        ptime.presentID = demo->next_present_id++;
        demo->prev_desired_present_time = ptime.desiredPresentTime;

        VkPresentTimesInfoGOOGLE present_time = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
            .pNext = present.pNext,
            .swapchainCount = present.swapchainCount,
            .pTimes = &ptime,
        };
        if (demo->VK_GOOGLE_display_timing_enabled) {
            present.pNext = &present_time;
        }
    }

#ifndef WIN32
    if (demo->waitMsecs >= 0) {
        usleep(demo->waitMsecs * 1000);
    }
    else {
        // Randomized wait time:
        usleep(-1000 * demo->waitMsecs * ((double) random() / (double) RAND_MAX));
    }
#else
    Sleep(demo->waitMsecs);
#endif

    uint64_t tPreSwapRequested = getTimeInNanoseconds();
    err = demo->fpQueuePresentKHR(demo->present_queue, &present);
    tPostSwapRequested = getTimeInNanoseconds();

    demo->frame_index += 1;
    demo->frame_index %= FRAME_LAG;

#if 0
    if (demo->fpGetSwapchainCounterEXT) {
    uint64_t counter;

    demo->fpGetSwapchainCounterEXT(demo->device,
                       demo->swapchain,
                       0,
                       &counter);
    }
#endif

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        // demo->swapchain is out of date (e.g. the window was resized) and
        // must be recreated:
        demo_resize(demo);
    } else if (err == VK_SUBOPTIMAL_KHR) {
        // demo->swapchain is not as optimal as it could be, but the platform's
        // presentation engine will still present the image correctly.
    } else {
        assert(!err);
    }

    if (false)
        printf("tPostSwapRequested - tPreSwapRequested = %f msecs. tPostSwapRequested - tSwapComplete = %f msecs.\n",
            (double)(tPostSwapRequested - tPreSwapRequested) / 1000000.0,
            (double)(tPostSwapRequested - tSwapComplete) / 1000000.0);
}

static void demo_prepare_buffers(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;
    VkSwapchainKHR oldSwapchain = demo->swapchain;

    // Check the surface capabilities and formats
    VkSurfaceCapabilitiesKHR surfCapabilities;
    err = demo->fpGetPhysicalDeviceSurfaceCapabilitiesKHR(
        demo->gpu, demo->surface, &surfCapabilities);
    assert(!err);

    uint32_t presentModeCount;
    err = demo->fpGetPhysicalDeviceSurfacePresentModesKHR(
        demo->gpu, demo->surface, &presentModeCount, NULL);
    assert(!err);
    VkPresentModeKHR *presentModes =
        (VkPresentModeKHR *)malloc(presentModeCount * sizeof(VkPresentModeKHR));
    assert(presentModes);
    err = demo->fpGetPhysicalDeviceSurfacePresentModesKHR(
        demo->gpu, demo->surface, &presentModeCount, presentModes);
    assert(!err);
    printf("%d present modes [0] = %d\n", presentModeCount, presentModes[0]);

    VkExtent2D swapchainExtent;
    // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
    if (surfCapabilities.currentExtent.width == 0xFFFFFFFF) {
        // If the surface size is undefined, the size is set to the size
        // of the images requested, which must fit within the minimum and
        // maximum values.
        swapchainExtent.width = demo->width;
        swapchainExtent.height = demo->height;

        if (swapchainExtent.width < surfCapabilities.minImageExtent.width) {
            swapchainExtent.width = surfCapabilities.minImageExtent.width;
        } else if (swapchainExtent.width > surfCapabilities.maxImageExtent.width) {
            swapchainExtent.width = surfCapabilities.maxImageExtent.width;
        }

        if (swapchainExtent.height < surfCapabilities.minImageExtent.height) {
            swapchainExtent.height = surfCapabilities.minImageExtent.height;
        } else if (swapchainExtent.height > surfCapabilities.maxImageExtent.height) {
            swapchainExtent.height = surfCapabilities.maxImageExtent.height;
        }
    } else {
        // If the surface size is defined, the swap chain size must match
        swapchainExtent = surfCapabilities.currentExtent;
        demo->width = surfCapabilities.currentExtent.width;
        demo->height = surfCapabilities.currentExtent.height;
        printf("Swapchain size: %i x %i\n", demo->width, demo->height);
    }

    // The FIFO present mode is guaranteed by the spec to be supported
    // and to have no tearing.  It's a great default present mode to use.
    VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

    //  There are times when you may wish to use another present mode.  The
    //  following code shows how to select them, and the comments provide some
    //  reasons you may wish to use them.
    //
    // It should be noted that Vulkan 1.0 doesn't provide a method for
    // synchronizing rendering with the presentation engine's display.  There
    // is a method provided for throttling rendering with the display, but
    // there are some presentation engines for which this method will not work.
    // If an application doesn't throttle its rendering, and if it renders much
    // faster than the refresh rate of the display, this can waste power on
    // mobile devices.  That is because power is being spent rendering images
    // that may never be seen.

    // VK_PRESENT_MODE_IMMEDIATE_KHR is for applications that don't care about
    // tearing, or have some way of synchronizing their rendering with the
    // display.
    // VK_PRESENT_MODE_MAILBOX_KHR may be useful for applications that
    // generally render a new presentable image every refresh cycle, but are
    // occasionally early.  In this case, the application wants the new image
    // to be displayed instead of the previously-queued-for-presentation image
    // that has not yet been displayed.
    // VK_PRESENT_MODE_FIFO_RELAXED_KHR is for applications that generally
    // render a new presentable image every refresh cycle, but are occasionally
    // late.  In this case (perhaps because of stuttering/latency concerns),
    // the application wants the late image to be immediately displayed, even
    // though that may mean some tearing.

    if (demo->presentMode !=  swapchainPresentMode) {

        for (size_t i = 0; i < presentModeCount; ++i) {
            if (presentModes[i] == demo->presentMode) {
                swapchainPresentMode = demo->presentMode;
                break;
            }
        }
    }
    if (swapchainPresentMode != demo->presentMode) {
        ERR_EXIT("Present mode specified is not supported\n", "Present mode unsupported");
    }

    // Determine the number of VkImages to use in the swap chain.
    // Application desires to acquire 2 images at a time for double
    // buffering: MK Changed to 2 for timestamping hack.
    uint32_t desiredNumOfSwapchainImages = 2;
    if (desiredNumOfSwapchainImages < surfCapabilities.minImageCount) {
        desiredNumOfSwapchainImages = surfCapabilities.minImageCount;
    }
    // If maxImageCount is 0, we can ask for as many images as we want;
    // otherwise we're limited to maxImageCount
    if ((surfCapabilities.maxImageCount > 0) &&
        (desiredNumOfSwapchainImages > surfCapabilities.maxImageCount)) {
        // Application must settle for fewer images than desired:
        desiredNumOfSwapchainImages = surfCapabilities.maxImageCount;
    }

    VkSurfaceTransformFlagsKHR preTransform;
    if (surfCapabilities.supportedTransforms &
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        preTransform = surfCapabilities.currentTransform;
    }

    // Find a supported composite alpha mode - one of these is guaranteed to be set
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[4] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (uint32_t i = 0; i < sizeof(compositeAlphaFlags); i++) {
        if (surfCapabilities.supportedCompositeAlpha & compositeAlphaFlags[i]) {
            compositeAlpha = compositeAlphaFlags[i];
            break;
        }
    }

#if defined(WIN32)
    VkSurfaceFullScreenExclusiveWin32InfoEXT fullscreen_exclusive_info_win32 = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT,
        .pNext = NULL,
        .hmonitor = MonitorFromWindow(demo->window, MONITOR_DEFAULTTOPRIMARY),
    };

    VkSurfaceFullScreenExclusiveInfoEXT fullscreen_exclusive_info = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT,
        .pNext = &fullscreen_exclusive_info_win32,
        .fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT,
    };
#endif

    VkSwapchainCreateInfoKHR swapchain_ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
#if defined(WIN32)
        .pNext = &fullscreen_exclusive_info,
#else
        .pNext = NULL,
#endif
        .surface = demo->surface,
        .minImageCount = desiredNumOfSwapchainImages,
        .imageFormat = demo->format,
        .imageColorSpace = demo->color_space,
        .imageExtent =
            {
             .width = swapchainExtent.width, .height = swapchainExtent.height,
            },
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = preTransform,
        .compositeAlpha = compositeAlpha,
        .imageArrayLayers = 1,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .presentMode = swapchainPresentMode,
        .oldSwapchain = oldSwapchain,
        .clipped = true,
    };
    uint32_t i;
    err = demo->fpCreateSwapchainKHR(demo->device, &swapchain_ci, NULL,
                                     &demo->swapchain);
    assert(!err);

    // If we just re-created an existing swapchain, we should destroy the old
    // swapchain at this point.
    // Note: destroying the swapchain also cleans up all its associated
    // presentable images once the platform is done with them.
    if (oldSwapchain != VK_NULL_HANDLE) {
        // AMD driver times out waiting on fences used in AcquireNextImage on
        // a swapchain that is subsequently destroyed before the wait.
        vkWaitForFences(demo->device, FRAME_LAG, demo->fences, VK_TRUE, UINT64_MAX);
//        vkResetFences(demo->device, FRAME_LAG, demo->fences);
        demo->fpDestroySwapchainKHR(demo->device, oldSwapchain, NULL);
    }

    err = demo->fpGetSwapchainImagesKHR(demo->device, demo->swapchain,
                                        &demo->swapchainImageCount, NULL);
    assert(!err);

    // MK Important for timestamping hack:
    if (demo->swapchainImageCount > desiredNumOfSwapchainImages) {
        printf("Got %i swapchain images, more than the desired %i ones. Clamping.\n",
               demo->swapchainImageCount, desiredNumOfSwapchainImages);
        demo->swapchainImageCount = desiredNumOfSwapchainImages;
    }

    VkImage *swapchainImages =
        (VkImage *)malloc(demo->swapchainImageCount * sizeof(VkImage));
    assert(swapchainImages);
    err = demo->fpGetSwapchainImagesKHR(demo->device, demo->swapchain,
                                        &demo->swapchainImageCount,
                                        swapchainImages);
    assert(err == VK_INCOMPLETE || err == VK_SUCCESS);

    demo->swapchain_image_resources = (SwapchainImageResources *)malloc(sizeof(SwapchainImageResources) *
                                               demo->swapchainImageCount);
    assert(demo->swapchain_image_resources);

    for (i = 0; i < demo->swapchainImageCount; i++) {
        VkImageViewCreateInfo color_image_view = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .format = demo->format,
            .components =
                {
                 .r = VK_COMPONENT_SWIZZLE_R,
                 .g = VK_COMPONENT_SWIZZLE_G,
                 .b = VK_COMPONENT_SWIZZLE_B,
                 .a = VK_COMPONENT_SWIZZLE_A,
                },
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1},
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .flags = 0,
        };

        demo->swapchain_image_resources[i].image = swapchainImages[i];

        color_image_view.image = demo->swapchain_image_resources[i].image;

        err = vkCreateImageView(demo->device, &color_image_view, NULL,
                                &demo->swapchain_image_resources[i].view);
        assert(!err);
    }

    if (demo->VK_GOOGLE_display_timing_enabled) {
        VkRefreshCycleDurationGOOGLE rc_dur;
        err = demo->fpGetRefreshCycleDurationGOOGLE(demo->device,
                                                    demo->swapchain,
                                                    &rc_dur);
        assert(!err);

        demo->syncd_with_actual_presents = false;
        // Initially target 1X the refresh duration:
        demo->refresh_duration_multiplier = 1;
        demo->target_IPD = DemoRefreshDuration(demo) * demo->refresh_duration_multiplier;

        demo->prev_desired_present_time = 0;
        demo->next_present_id = 1;
    }

    if (NULL != presentModes) {
        free(presentModes);
    }

#if defined(WIN32)
    err = demo->fpAcquireFullScreenExclusiveModeEXT(demo->device, demo->swapchain);
    switch (err) {
        case VK_SUCCESS:
            printf("Switched to fullscreen exclusive mode.\n");
            break;

        case VK_ERROR_INITIALIZATION_FAILED:
            printf("Could not switch to fullscreen exclusive mode.\n");
            break;

        default:
            printf("Error during switch to fullscreen exclusive mode.\n");
            break;
    }
#endif

    if (demo->hdr_enabled) {
        if (demo->fpSetLocalDimmingAMD)
            demo->fpSetLocalDimmingAMD(demo->device, demo->swapchain, demo->local_dimming_enabled);
    }
}

static void demo_prepare_depth(struct demo *demo) {
    const VkFormat depth_format = VK_FORMAT_D16_UNORM;
    const VkImageCreateInfo image = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format,
        .extent = {demo->width, demo->height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .flags = 0,
    };

    VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = NULL,
        .image = VK_NULL_HANDLE,
        .format = depth_format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .flags = 0,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
    };

    VkMemoryRequirements mem_reqs;
    VkResult U_ASSERT_ONLY err;
    bool U_ASSERT_ONLY pass;

    demo->depth.format = depth_format;

    /* create image */
    err = vkCreateImage(demo->device, &image, NULL, &demo->depth.image);
    assert(!err);

    vkGetImageMemoryRequirements(demo->device, demo->depth.image, &mem_reqs);
    assert(!err);

    demo->depth.mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    demo->depth.mem_alloc.pNext = NULL;
    demo->depth.mem_alloc.allocationSize = mem_reqs.size;
    demo->depth.mem_alloc.memoryTypeIndex = 0;

    pass = memory_type_from_properties(demo, mem_reqs.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       &demo->depth.mem_alloc.memoryTypeIndex);
    assert(pass);

    /* allocate memory */
    err = vkAllocateMemory(demo->device, &demo->depth.mem_alloc, NULL,
                           &demo->depth.mem);
    assert(!err);

    /* bind memory */
    err =
        vkBindImageMemory(demo->device, demo->depth.image, demo->depth.mem, 0);
    assert(!err);

    /* create image view */
    view.image = demo->depth.image;
    err = vkCreateImageView(demo->device, &view, NULL, &demo->depth.view);
    assert(!err);
}

/* Load a ppm file into memory */
bool loadTexture(const char *filename, uint8_t *rgba_data,
                 VkSubresourceLayout *layout, int32_t *width, int32_t *height) {

#if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
    filename =[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: @(filename)].UTF8String;
#endif

#ifdef __ANDROID__
#include <lunarg.ppm.h>
    char *cPtr;
    cPtr = (char*)lunarg_ppm;
    if ((unsigned char*)cPtr >= (lunarg_ppm + lunarg_ppm_len) || strncmp(cPtr, "P6\n", 3)) {
        return false;
    }
    while(strncmp(cPtr++, "\n", 1));
    sscanf(cPtr, "%u %u", width, height);
    if (rgba_data == NULL) {
        return true;
    }
    while(strncmp(cPtr++, "\n", 1));
    if ((unsigned char*)cPtr >= (lunarg_ppm + lunarg_ppm_len) || strncmp(cPtr, "255\n", 4)) {
        return false;
    }
    while(strncmp(cPtr++, "\n", 1));

    for (int y = 0; y < *height; y++) {
        uint8_t *rowPtr = rgba_data;
        for (int x = 0; x < *width; x++) {
            memcpy(rowPtr, cPtr, 3);
            rowPtr[3] = 255; /* Alpha of 1 */
            rowPtr += 4;
            cPtr += 3;
        }
        rgba_data += layout->rowPitch;
    }

    return true;
#else
    FILE *fPtr = fopen(filename, "rb");
    char header[256], *cPtr, *tmp;

    if (!fPtr)
        return false;

    cPtr = fgets(header, 256, fPtr); // P6
    if (cPtr == NULL || strncmp(header, "P6\n", 3)) {
        fclose(fPtr);
        return false;
    }

    do {
        cPtr = fgets(header, 256, fPtr);
        if (cPtr == NULL) {
            fclose(fPtr);
            return false;
        }
    } while (!strncmp(header, "#", 1));

    sscanf(header, "%u %u", width, height);
    if (rgba_data == NULL) {
        fclose(fPtr);
        return true;
    }
    tmp = fgets(header, 256, fPtr); // Format
    (void)tmp;
    if (cPtr == NULL || strncmp(header, "255\n", 3)) {
        fclose(fPtr);
        return false;
    }

    for (int y = 0; y < *height; y++) {
        uint8_t *rowPtr = rgba_data;
        for (int x = 0; x < *width; x++) {
            size_t s = fread(rowPtr, 3, 1, fPtr);
            (void)s;
            rowPtr[3] = 255; /* Alpha of 1 */
            rowPtr += 4;
        }
        rgba_data += layout->rowPitch;
    }
    fclose(fPtr);
    return true;
#endif
}

static void demo_prepare_texture_image(struct demo *demo, const char *filename,
                                       struct texture_object *tex_obj,
                                       VkImageTiling tiling,
                                       VkImageUsageFlags usage,
                                       VkFlags required_props) {
    const VkFormat tex_format = demo->interop_tex_format;
    int32_t tex_width;
    int32_t tex_height;
    VkResult U_ASSERT_ONLY err;
    bool U_ASSERT_ONLY pass;

    // No OpenGL interop?
    if (required_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        // Get needed texture size for image:
        if (!loadTexture(filename, NULL, NULL, &tex_width, &tex_height)) {
            ERR_EXIT("Failed to load textures", "Load Texture Failure");
        }
    }
    else {
        // OpenGL interop: Allocate interop texture the size of the true
        // framebuffer:
        tex_width = demo->width;
        tex_height = demo->height;
    }

    tex_obj->tex_width = tex_width;
    tex_obj->tex_height = tex_height;

    const VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = tex_format,
        .extent = {tex_width, tex_height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .flags = 0,
        .initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };

    VkMemoryRequirements mem_reqs;

    err = vkCreateImage(demo->device, &image_create_info, NULL, &tex_obj->image);
    assert(!err);

    vkGetImageMemoryRequirements(demo->device, tex_obj->image, &mem_reqs);

    VkExportMemoryAllocateInfo exportAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
#if defined(WIN32)
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
#else
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
#endif
    };

    tex_obj->mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    tex_obj->mem_alloc.pNext = (demo->interop_enabled) ? &exportAllocInfo : NULL;
    tex_obj->mem_alloc.allocationSize = mem_reqs.size;
    tex_obj->mem_alloc.memoryTypeIndex = 0;

    pass = memory_type_from_properties(demo, mem_reqs.memoryTypeBits,
                                       required_props,
                                       &tex_obj->mem_alloc.memoryTypeIndex);
    assert(pass);

    /* allocate memory */
    err = vkAllocateMemory(demo->device, &tex_obj->mem_alloc, NULL,
                           &(tex_obj->mem));
    assert(!err);

    /* bind memory */
    err = vkBindImageMemory(demo->device, tex_obj->image, tex_obj->mem, 0);
    assert(!err);

    memset(&demo->interophandles, 0, sizeof(demo->interophandles));

    if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && demo->interop_enabled) {
#ifdef WIN32
        // Get handle for shared memory with OpenGL:
        VkMemoryGetWin32HandleInfoKHR memorygetwinhandleinfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
            .pNext = NULL,
            .memory = tex_obj->mem,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };

        //printf("PRE memory handle %p\n", demo->interophandles.memory);
        err = demo->fpGetMemoryWin32HandleKHR(demo->device, &memorygetwinhandleinfo, &demo->interophandles.memory);
        assert(!err);
        printf("GOT memory handle %p\n", demo->interophandles.memory);
#else
        // Get fd for shared memory with OpenGL:
        VkMemoryGetFdInfoKHR memorygetfdinfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .pNext = NULL,
            .memory = tex_obj->mem,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        };

        //printf("PRE memory fd %i\n", demo->interophandles.memory);
        err = demo->fpGetMemoryFdKHR(demo->device, &memorygetfdinfo, &demo->interophandles.memory);
        assert(!err);
        printf("GOT memory fd %i\n", demo->interophandles.memory);
#endif
    }

    if (required_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        const VkImageSubresource subres = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .arrayLayer = 0,
        };
        VkSubresourceLayout layout;
        void *data;

        vkGetImageSubresourceLayout(demo->device, tex_obj->image, &subres,
                                    &layout);

        err = vkMapMemory(demo->device, tex_obj->mem, 0,
                          tex_obj->mem_alloc.allocationSize, 0, &data);
        assert(!err);

        printf("Vulkan texture image row pitch is %i bytes.\n", (int) layout.rowPitch);

        if (!loadTexture(filename, data, &layout, &tex_width, &tex_height)) {
            fprintf(stderr, "Error loading texture: %s\n", filename);
        }

        vkUnmapMemory(demo->device, tex_obj->mem);
    }

    // MK tex_obj->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    tex_obj->imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

static void demo_destroy_texture_image(struct demo *demo,
                                       struct texture_object *tex_objs) {
    /* clean up staging resources */
    vkFreeMemory(demo->device, tex_objs->mem, NULL);
    vkDestroyImage(demo->device, tex_objs->image, NULL);
}

static void demo_prepare_textures(struct demo *demo) {
    const VkFormat tex_format = demo->interop_tex_format;
    VkFormatProperties props;
    uint32_t i;

    vkGetPhysicalDeviceFormatProperties(demo->gpu, tex_format, &props);

    for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
        VkResult U_ASSERT_ONLY err;

        // MK: Need linear tiling for OpenGL interop on AMD:
        if (((props.linearTilingFeatures &
            (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) == (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) &&
            !demo->use_staging_buffer &&
            !demo->interop_tiled_texture) {
            demo->interop_tiled_texture = false;
            printf("Will use linear textures for OpenGL->Vulkan interop via render-to-texture to texture %i\n", i);
            /* Device can texture using linear textures */
            demo_prepare_texture_image(
                demo, tex_files[i], &demo->textures[i], VK_IMAGE_TILING_LINEAR,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | ((i == 0) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0),
                ((!demo->interop_enabled) ?
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT :
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)); // MK Require device local bit.

            // Nothing in the pipeline needs to be complete to start, and don't allow fragment
            // shader to run until layout transition completes
            demo_set_image_layout(demo, demo->textures[i].image, VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_PREINITIALIZED, demo->textures[i].imageLayout,
                                  VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, NULL);

            demo->staging_texture.image = 0;
        } else if ((props.optimalTilingFeatures &
                (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) == (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            /* Must use staging buffer to copy linear texture to optimized */
            demo->interop_tiled_texture = true;
            printf("Will use optimal tiled textures for OpenGL->Vulkan interop via render-to-texture to texture %i\n", i);
            memset(&demo->staging_texture, 0, sizeof(demo->staging_texture));
            demo_prepare_texture_image(
                demo, tex_files[i], &demo->staging_texture, VK_IMAGE_TILING_LINEAR,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            demo_prepare_texture_image(
                demo, tex_files[i], &demo->textures[i], VK_IMAGE_TILING_OPTIMAL,
                (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                ((i == 0) ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0)),
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);  // MK Require device local bit.

            demo_set_image_layout(demo, demo->staging_texture.image,
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_PREINITIALIZED,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_HOST_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, NULL);

            demo_set_image_layout(demo, demo->textures[i].image,
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_PREINITIALIZED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_ACCESS_HOST_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, NULL);

            VkImageCopy copy_region = {
                .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .srcOffset = {0, 0, 0},
                .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .dstOffset = {0, 0, 0},
                .extent = {demo->staging_texture.tex_width,
                           demo->staging_texture.tex_height, 1},
            };
            vkCmdCopyImage(
                demo->cmd, demo->staging_texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, demo->textures[i].image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

            demo_set_image_layout(demo, demo->textures[i].image,
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  demo->textures[i].imageLayout,
                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, NULL);

        } else {
            /* Can't support VK_FORMAT_R8G8B8A8_UNORM !? */
            assert(!"No support for R8G8B8A8_UNORM as texture image format");
        }

        const VkSamplerCreateInfo sampler = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = NULL,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1,
            .compareOp = VK_COMPARE_OP_NEVER,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
            .unnormalizedCoordinates = VK_FALSE,
        };

        VkImageViewCreateInfo view = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .image = VK_NULL_HANDLE,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = tex_format,
            .components =
                {
                 VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                 VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
                },
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .flags = 0,
        };

        /* create sampler */
        err = vkCreateSampler(demo->device, &sampler, NULL,
                              &demo->textures[i].sampler);
        assert(!err);

        /* create image view */
        view.image = demo->textures[i].image;
        err = vkCreateImageView(demo->device, &view, NULL,
                                &demo->textures[i].view);
        assert(!err);
    }
}

void demo_prepare_cube_data_buffers(struct demo *demo) {
    VkBufferCreateInfo buf_info;
    VkMemoryRequirements mem_reqs;
    VkMemoryAllocateInfo mem_alloc;
    uint8_t *pData;
    mat4x4 MVP, VP;
    VkResult U_ASSERT_ONLY err;
    bool U_ASSERT_ONLY pass;
    struct vktexcube_vs_uniform data;

    mat4x4_mul(VP, demo->projection_matrix, demo->view_matrix);
    mat4x4_mul(MVP, VP, demo->model_matrix);
    memcpy(data.mvp, MVP, sizeof(MVP));
    //    dumpMatrix("MVP", MVP);

    for (unsigned int i = 0; i < 2 * 3; i++) {
        data.position[i][0] = g_vertex_buffer_data[i * 3];
        data.position[i][1] = g_vertex_buffer_data[i * 3 + 1];
        data.position[i][2] = g_vertex_buffer_data[i * 3 + 2];
        data.position[i][3] = 1.0f;
        data.attr[i][0] = g_uv_buffer_data[2 * i];
        data.attr[i][1] = g_uv_buffer_data[2 * i + 1];
        data.attr[i][2] = 0;
        data.attr[i][3] = 0;
    }

    memset(&buf_info, 0, sizeof(buf_info));
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buf_info.size = sizeof(data);

    for (unsigned int i = 0; i < demo->swapchainImageCount; i++) {
        err =
            vkCreateBuffer(demo->device, &buf_info, NULL,
                           &demo->swapchain_image_resources[i].uniform_buffer);
        assert(!err);

        vkGetBufferMemoryRequirements(demo->device,
                                      demo->swapchain_image_resources[i].uniform_buffer,
                                      &mem_reqs);

        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.pNext = NULL;
        mem_alloc.allocationSize = mem_reqs.size;
        mem_alloc.memoryTypeIndex = 0;

        pass = memory_type_from_properties(
            demo, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &mem_alloc.memoryTypeIndex);
        assert(pass);

        err = vkAllocateMemory(demo->device, &mem_alloc, NULL,
                           &demo->swapchain_image_resources[i].uniform_memory);
        assert(!err);

        err = vkMapMemory(demo->device, demo->swapchain_image_resources[i].uniform_memory, 0,
                      VK_WHOLE_SIZE, 0, (void **)&pData);
        assert(!err);

        memcpy(pData, &data, sizeof data);

        vkUnmapMemory(demo->device, demo->swapchain_image_resources[i].uniform_memory);

        err = vkBindBufferMemory(demo->device, demo->swapchain_image_resources[i].uniform_buffer,
                             demo->swapchain_image_resources[i].uniform_memory, 0);
        assert(!err);
    }
}

static void demo_prepare_descriptor_layout(struct demo *demo) {
    const VkDescriptorSetLayoutBinding layout_bindings[2] = {
            [0] =
                {
                 .binding = 0,
                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .descriptorCount = 1,
                 .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                 .pImmutableSamplers = NULL,
                },
            [1] =
                {
                 .binding = 1,
                 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .descriptorCount = DEMO_TEXTURE_COUNT,
                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                 .pImmutableSamplers = NULL,
                },
    };
    const VkDescriptorSetLayoutCreateInfo descriptor_layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .bindingCount = 2,
        .pBindings = layout_bindings,
    };
    VkResult U_ASSERT_ONLY err;

    err = vkCreateDescriptorSetLayout(demo->device, &descriptor_layout, NULL,
                                      &demo->desc_layout);
    assert(!err);

    const VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .setLayoutCount = 1,
        .pSetLayouts = &demo->desc_layout,
    };

    err = vkCreatePipelineLayout(demo->device, &pPipelineLayoutCreateInfo, NULL,
                                 &demo->pipeline_layout);
    assert(!err);
}

static void demo_prepare_render_pass(struct demo *demo) {
    // The initial layout for the color and depth attachments will be LAYOUT_UNDEFINED
    // because at the start of the renderpass, we don't care about their contents.
    // At the start of the subpass, the color attachment's layout will be transitioned
    // to LAYOUT_COLOR_ATTACHMENT_OPTIMAL and the depth stencil attachment's layout
    // will be transitioned to LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL.  At the end of
    // the renderpass, the color attachment's layout will be transitioned to
    // LAYOUT_PRESENT_SRC_KHR to be ready to present.  This is all done as part of
    // the renderpass, no barriers are necessary.
    const VkAttachmentDescription attachments[2] = {
            [0] =
                {
                 .format = demo->format,
                 .flags = 0,
                 .samples = VK_SAMPLE_COUNT_1_BIT,
                 .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                 .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                 .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                 .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                 .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                 .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                },
            [1] =
                {
                 .format = demo->depth.format,
                 .flags = 0,
                 .samples = VK_SAMPLE_COUNT_1_BIT,
                 .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                 .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                 .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                 .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                 .initialLayout =
                     VK_IMAGE_LAYOUT_UNDEFINED,
                 .finalLayout =
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                },
    };
    const VkAttachmentReference color_reference = {
        .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkAttachmentReference depth_reference = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .flags = 0,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
        .pResolveAttachments = NULL,
        .pDepthStencilAttachment = &depth_reference,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = NULL,
    };
    const VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = NULL,
    };
    VkResult U_ASSERT_ONLY err;

    err = vkCreateRenderPass(demo->device, &rp_info, NULL, &demo->render_pass);
    assert(!err);
}

//TODO: Merge shader reading
#ifndef __ANDROID__
static VkShaderModule
demo_prepare_shader_module(struct demo *demo, const void *code, size_t size) {
    VkShaderModule module;
    VkShaderModuleCreateInfo moduleCreateInfo;
    VkResult U_ASSERT_ONLY err;

    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.pNext = NULL;

    moduleCreateInfo.codeSize = size;
    moduleCreateInfo.pCode = code;
    moduleCreateInfo.flags = 0;
    err = vkCreateShaderModule(demo->device, &moduleCreateInfo, NULL, &module);
    assert(!err);

    return module;
}

char *demo_read_spv(const char *filename, size_t *psize) {
    long int size;
    size_t U_ASSERT_ONLY retval;
    void *shader_code;

#if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
    filename =[[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: @(filename)].UTF8String;
#endif

    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return NULL;

    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);

    fseek(fp, 0L, SEEK_SET);

    shader_code = malloc(size);
    retval = fread(shader_code, size, 1, fp);
    assert(retval == 1);

    *psize = size;

    fclose(fp);
    return shader_code;
}
#endif

static VkShaderModule demo_prepare_vs(struct demo *demo) {
#ifdef __ANDROID__
    VkShaderModuleCreateInfo sh_info = {};
    sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "cube.vert.h"
    sh_info.codeSize = sizeof(cube_vert);
    sh_info.pCode = cube_vert;
    VkResult U_ASSERT_ONLY err = vkCreateShaderModule(demo->device, &sh_info, NULL, &demo->vert_shader_module);
    assert(!err);
#else
    void *vertShaderCode;
    size_t size;

    vertShaderCode = demo_read_spv("cube-vert.spv", &size);
    if (!vertShaderCode) {
        ERR_EXIT("Failed to load cube-vert.spv", "Load Shader Failure");
    }

    demo->vert_shader_module =
        demo_prepare_shader_module(demo, vertShaderCode, size);

    free(vertShaderCode);
#endif

    return demo->vert_shader_module;
}

static VkShaderModule demo_prepare_fs(struct demo *demo) {
#ifdef __ANDROID__
    VkShaderModuleCreateInfo sh_info = {};
    sh_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

#include "cube.frag.h"
    sh_info.codeSize = sizeof(cube_frag);
    sh_info.pCode = cube_frag;
    VkResult U_ASSERT_ONLY err = vkCreateShaderModule(demo->device, &sh_info, NULL, &demo->frag_shader_module);
    assert(!err);
#else
    void *fragShaderCode;
    size_t size;

    fragShaderCode = demo_read_spv("cube-frag.spv", &size);
    if (!fragShaderCode) {
        ERR_EXIT("Failed to load cube-frag.spv", "Load Shader Failure");
    }

    demo->frag_shader_module =
        demo_prepare_shader_module(demo, fragShaderCode, size);

    free(fragShaderCode);
#endif

    return demo->frag_shader_module;
}

static void demo_prepare_pipeline(struct demo *demo) {
    VkGraphicsPipelineCreateInfo pipeline;
    VkPipelineCacheCreateInfo pipelineCache;
    VkPipelineVertexInputStateCreateInfo vi;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineMultisampleStateCreateInfo ms;
    VkDynamicState dynamicStateEnables[VK_DYNAMIC_STATE_RANGE_SIZE];
    VkPipelineDynamicStateCreateInfo dynamicState;
    VkResult U_ASSERT_ONLY err;

    memset(dynamicStateEnables, 0, sizeof dynamicStateEnables);
    memset(&dynamicState, 0, sizeof dynamicState);
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pDynamicStates = dynamicStateEnables;

    memset(&pipeline, 0, sizeof(pipeline));
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.layout = demo->pipeline_layout;

    memset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.depthClampEnable = VK_FALSE;
    rs.rasterizerDiscardEnable = VK_FALSE;
    rs.depthBiasEnable = VK_FALSE;
    rs.lineWidth = 1.0f;

    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState att_state[1];
    memset(att_state, 0, sizeof(att_state));
    att_state[0].colorWriteMask = 0xf;
    att_state[0].blendEnable = VK_FALSE;
    cb.attachmentCount = 1;
    cb.pAttachments = att_state;

    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    dynamicStateEnables[dynamicState.dynamicStateCount++] =
        VK_DYNAMIC_STATE_VIEWPORT;
    vp.scissorCount = 1;
    dynamicStateEnables[dynamicState.dynamicStateCount++] =
        VK_DYNAMIC_STATE_SCISSOR;

    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.back.failOp = VK_STENCIL_OP_KEEP;
    ds.back.passOp = VK_STENCIL_OP_KEEP;
    ds.back.compareOp = VK_COMPARE_OP_ALWAYS;
    ds.stencilTestEnable = VK_FALSE;
    ds.front = ds.back;

    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.pSampleMask = NULL;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Two stages: vs and fs
    pipeline.stageCount = 2;
    VkPipelineShaderStageCreateInfo shaderStages[2];
    memset(&shaderStages, 0, 2 * sizeof(VkPipelineShaderStageCreateInfo));

    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = demo_prepare_vs(demo);
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = demo_prepare_fs(demo);
    shaderStages[1].pName = "main";

    memset(&pipelineCache, 0, sizeof(pipelineCache));
    pipelineCache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    err = vkCreatePipelineCache(demo->device, &pipelineCache, NULL,
                                &demo->pipelineCache);
    assert(!err);

    pipeline.pVertexInputState = &vi;
    pipeline.pInputAssemblyState = &ia;
    pipeline.pRasterizationState = &rs;
    pipeline.pColorBlendState = &cb;
    pipeline.pMultisampleState = &ms;
    pipeline.pViewportState = &vp;
    pipeline.pDepthStencilState = &ds;
    pipeline.pStages = shaderStages;
    pipeline.renderPass = demo->render_pass;
    pipeline.pDynamicState = &dynamicState;

    pipeline.renderPass = demo->render_pass;

    err = vkCreateGraphicsPipelines(demo->device, demo->pipelineCache, 1,
                                    &pipeline, NULL, &demo->pipeline);
    assert(!err);

    vkDestroyShaderModule(demo->device, demo->frag_shader_module, NULL);
    vkDestroyShaderModule(demo->device, demo->vert_shader_module, NULL);
}

static void demo_prepare_descriptor_pool(struct demo *demo) {
    const VkDescriptorPoolSize type_counts[2] = {
            [0] =
                {
                 .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .descriptorCount = demo->swapchainImageCount,
                },
            [1] =
                {
                 .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                 .descriptorCount = demo->swapchainImageCount * DEMO_TEXTURE_COUNT,
                },
    };
    const VkDescriptorPoolCreateInfo descriptor_pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .maxSets = demo->swapchainImageCount,
        .poolSizeCount = 2,
        .pPoolSizes = type_counts,
    };
    VkResult U_ASSERT_ONLY err;

    err = vkCreateDescriptorPool(demo->device, &descriptor_pool, NULL,
                                 &demo->desc_pool);
    assert(!err);
}

static void demo_prepare_descriptor_set(struct demo *demo) {
    VkDescriptorImageInfo tex_descs[DEMO_TEXTURE_COUNT];
    VkWriteDescriptorSet writes[2];
    VkResult U_ASSERT_ONLY err;

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = demo->desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &demo->desc_layout};

    VkDescriptorBufferInfo buffer_info;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(struct vktexcube_vs_uniform);

    memset(&tex_descs, 0, sizeof(tex_descs));
    for (unsigned int i = 0; i < DEMO_TEXTURE_COUNT; i++) {
        tex_descs[i].sampler = demo->textures[i].sampler;
        tex_descs[i].imageView = demo->textures[i].view;
        tex_descs[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    memset(&writes, 0, sizeof(writes));

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &buffer_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = DEMO_TEXTURE_COUNT;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = tex_descs;

    for (unsigned int i = 0; i < demo->swapchainImageCount; i++) {
        err = vkAllocateDescriptorSets(demo->device, &alloc_info, &demo->swapchain_image_resources[i].descriptor_set);
        assert(!err);
        buffer_info.buffer = demo->swapchain_image_resources[i].uniform_buffer;
        writes[0].dstSet = demo->swapchain_image_resources[i].descriptor_set;
        writes[1].dstSet = demo->swapchain_image_resources[i].descriptor_set;
        vkUpdateDescriptorSets(demo->device, 2, writes, 0, NULL);
    }
}

static void demo_prepare_framebuffers(struct demo *demo) {
    VkImageView attachments[2];
    attachments[1] = demo->depth.view;

    const VkFramebufferCreateInfo fb_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = NULL,
        .renderPass = demo->render_pass,
        .attachmentCount = 2,
        .pAttachments = attachments,
        .width = demo->width,
        .height = demo->height,
        .layers = 1,
    };
    VkResult U_ASSERT_ONLY err;
    uint32_t i;

    for (i = 0; i < demo->swapchainImageCount; i++) {
        attachments[0] = demo->swapchain_image_resources[i].view;
        err = vkCreateFramebuffer(demo->device, &fb_info, NULL,
                                  &demo->swapchain_image_resources[i].framebuffer);
        assert(!err);
    }
}

static void demo_prepare(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;

    const VkCommandPoolCreateInfo cmd_pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .queueFamilyIndex = demo->graphics_queue_family_index,
        .flags = 0,
    };
    err = vkCreateCommandPool(demo->device, &cmd_pool_info, NULL,
                              &demo->cmd_pool);
    assert(!err);

    const VkCommandBufferAllocateInfo cmd = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = demo->cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    err = vkAllocateCommandBuffers(demo->device, &cmd, &demo->cmd);
    assert(!err);
    VkCommandBufferBeginInfo cmd_buf_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL,
    };
    err = vkBeginCommandBuffer(demo->cmd, &cmd_buf_info);
    assert(!err);

    demo_prepare_buffers(demo);
    demo_prepare_depth(demo);
    demo_prepare_textures(demo);
    demo_prepare_cube_data_buffers(demo);

    demo_prepare_descriptor_layout(demo);
    demo_prepare_render_pass(demo);
    demo_prepare_pipeline(demo);

    for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
        err =
            vkAllocateCommandBuffers(demo->device, &cmd, &demo->swapchain_image_resources[i].cmd);
        assert(!err);
    }

    if (demo->separate_present_queue) {
        const VkCommandPoolCreateInfo present_cmd_pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .queueFamilyIndex = demo->present_queue_family_index,
            .flags = 0,
        };
        err = vkCreateCommandPool(demo->device, &present_cmd_pool_info, NULL,
                                  &demo->present_cmd_pool);
        assert(!err);
        const VkCommandBufferAllocateInfo present_cmd_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = NULL,
            .commandPool = demo->present_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
            err = vkAllocateCommandBuffers(
                demo->device, &present_cmd_info, &demo->swapchain_image_resources[i].graphics_to_present_cmd);
            assert(!err);
            demo_build_image_ownership_cmd(demo, i);
        }
    }

    demo_prepare_descriptor_pool(demo);
    demo_prepare_descriptor_set(demo);

    demo_prepare_framebuffers(demo);

    for (uint32_t i = 0; i < demo->swapchainImageCount; i++) {
        demo->current_buffer = i;
        demo_draw_build_cmd(demo, demo->swapchain_image_resources[i].cmd);
    }

    /*
     * Prepare functions above may generate pipeline commands
     * that need to be flushed before beginning the render loop.
     */
    demo_flush_init_cmd(demo);
    if (demo->staging_texture.image) {
        demo_destroy_texture_image(demo, &demo->staging_texture);
    }

    demo->current_buffer = 0;
    demo->prepared = true;
}

static void demo_cleanup(struct demo *demo) {
    uint32_t i;

    demo->prepared = false;
    vkDeviceWaitIdle(demo->device);

    // Wait for fences from present operations
    for (i = 0; i < FRAME_LAG; i++) {
        vkWaitForFences(demo->device, 1, &demo->fences[i], VK_TRUE, UINT64_MAX);
        vkDestroyFence(demo->device, demo->fences[i], NULL);
        vkDestroySemaphore(demo->device, demo->image_acquired_semaphores[i], NULL);
        vkDestroySemaphore(demo->device, demo->draw_complete_semaphores[i], NULL);
        if (demo->separate_present_queue) {
            vkDestroySemaphore(demo->device, demo->image_ownership_semaphores[i], NULL);
        }
    }

    for (i = 0; i < demo->swapchainImageCount; i++) {
        vkDestroyFramebuffer(demo->device, demo->swapchain_image_resources[i].framebuffer, NULL);
    }
    vkDestroyDescriptorPool(demo->device, demo->desc_pool, NULL);

    vkDestroyPipeline(demo->device, demo->pipeline, NULL);
    vkDestroyPipelineCache(demo->device, demo->pipelineCache, NULL);
    vkDestroyRenderPass(demo->device, demo->render_pass, NULL);
    vkDestroyPipelineLayout(demo->device, demo->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(demo->device, demo->desc_layout, NULL);

    for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
        vkDestroyImageView(demo->device, demo->textures[i].view, NULL);
        vkDestroyImage(demo->device, demo->textures[i].image, NULL);
        vkFreeMemory(demo->device, demo->textures[i].mem, NULL);
        vkDestroySampler(demo->device, demo->textures[i].sampler, NULL);
    }
    demo->fpDestroySwapchainKHR(demo->device, demo->swapchain, NULL);

    vkDestroyImageView(demo->device, demo->depth.view, NULL);
    vkDestroyImage(demo->device, demo->depth.image, NULL);
    vkFreeMemory(demo->device, demo->depth.mem, NULL);

    for (i = 0; i < demo->swapchainImageCount; i++) {
        vkDestroyImageView(demo->device, demo->swapchain_image_resources[i].view, NULL);
        vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1,
                             &demo->swapchain_image_resources[i].cmd);
        vkDestroyBuffer(demo->device, demo->swapchain_image_resources[i].uniform_buffer, NULL);
        vkFreeMemory(demo->device, demo->swapchain_image_resources[i].uniform_memory, NULL);
    }
    free(demo->swapchain_image_resources);
    free(demo->queue_props);
    vkDestroyCommandPool(demo->device, demo->cmd_pool, NULL);

    if (demo->separate_present_queue) {
        vkDestroyCommandPool(demo->device, demo->present_cmd_pool, NULL);
    }
    vkDeviceWaitIdle(demo->device);

    // Release display from direct display mode under Linux:
    PFN_vkReleaseDisplayEXT m_pvkReleaseDisplayEXT = (PFN_vkReleaseDisplayEXT) vkGetInstanceProcAddr(demo->inst, "vkReleaseDisplayEXT" );
    if (m_pvkReleaseDisplayEXT)
        m_pvkReleaseDisplayEXT(demo->gpu, demo->vkdisplay);

    vkDestroyDevice(demo->device, NULL);
    if (demo->validate) {
        demo->DestroyDebugReportCallback(demo->inst, demo->msg_callback, NULL);
    }
    vkDestroySurfaceKHR(demo->inst, demo->surface, NULL);
    vkDestroyInstance(demo->inst, NULL);

#if defined(VK_USE_PLATFORM_XLIB_KHR)
    XDestroyWindow(demo->display, demo->xlib_window);
    XCloseDisplay(demo->display);
#elif defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)
    printf("Bye bye!\n");
    xcb_disconnect(demo->connection);
    free(demo->atom_wm_delete_window);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    wl_shell_surface_destroy(demo->shell_surface);
    wl_surface_destroy(demo->window);
    wl_shell_destroy(demo->shell);
    wl_compositor_destroy(demo->compositor);
    wl_registry_destroy(demo->registry);
    wl_display_disconnect(demo->display);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#endif
}

// Forward define:
static void demo_create_opengl_interop(struct demo* demo);

static void demo_resize(struct demo *demo) {
    uint32_t i;

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    // MK: Not needed for direct display mode:
    return;
#endif

    // Don't react to resize until after first initialization.
    if (!demo->prepared) {
        return;
    }
    // In order to properly resize the window, we must re-create the swapchain
    // AND redo the command buffers, etc.
    //
    // First, perform part of the demo_cleanup() function:
    demo->prepared = false;
    vkDeviceWaitIdle(demo->device);

    for (i = 0; i < demo->swapchainImageCount; i++) {
        vkDestroyFramebuffer(demo->device, demo->swapchain_image_resources[i].framebuffer, NULL);
    }
    vkDestroyDescriptorPool(demo->device, demo->desc_pool, NULL);

    vkDestroyPipeline(demo->device, demo->pipeline, NULL);
    vkDestroyPipelineCache(demo->device, demo->pipelineCache, NULL);
    vkDestroyRenderPass(demo->device, demo->render_pass, NULL);
    vkDestroyPipelineLayout(demo->device, demo->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(demo->device, demo->desc_layout, NULL);

    for (i = 0; i < DEMO_TEXTURE_COUNT; i++) {
        vkDestroyImageView(demo->device, demo->textures[i].view, NULL);
        vkDestroyImage(demo->device, demo->textures[i].image, NULL);
        vkFreeMemory(demo->device, demo->textures[i].mem, NULL);
        vkDestroySampler(demo->device, demo->textures[i].sampler, NULL);
    }

    vkDestroyImageView(demo->device, demo->depth.view, NULL);
    vkDestroyImage(demo->device, demo->depth.image, NULL);
    vkFreeMemory(demo->device, demo->depth.mem, NULL);

    for (i = 0; i < demo->swapchainImageCount; i++) {
        vkDestroyImageView(demo->device, demo->swapchain_image_resources[i].view, NULL);
        vkFreeCommandBuffers(demo->device, demo->cmd_pool, 1,
                             &demo->swapchain_image_resources[i].cmd);
        vkDestroyBuffer(demo->device, demo->swapchain_image_resources[i].uniform_buffer, NULL);
        vkFreeMemory(demo->device, demo->swapchain_image_resources[i].uniform_memory, NULL);
    }
    vkDestroyCommandPool(demo->device, demo->cmd_pool, NULL);
    if (demo->separate_present_queue) {
        vkDestroyCommandPool(demo->device, demo->present_cmd_pool, NULL);
    }
    free(demo->swapchain_image_resources);

    // Second, re-perform the demo_prepare() function, which will re-create the
    // swapchain:
    demo_prepare(demo);

    // Renitialize OpenGL side of OpenGL->Vulkan interop.
    demo_create_opengl_interop(demo);
}

// Simulated OpenGL rendering code -- would correspond to PTB user drawing code:
void draw_jesse(struct demo* demo)
{
    // Draw Jesse the cat - i assume?
    static bool firsttime = true;

    float maxL = (demo->nativeDisplayHdrMetadata.maxLuminance > 0.0) ? demo->nativeDisplayHdrMetadata.maxLuminance : 600;

    if (firsttime) {
        firsttime = false;

        // Default background color to maxFALL:
        if (demo->rgb[0] == -1 && demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel > 0.0)
            demo->rgb[0] = demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel;

        if (demo->rgb[1] == -1 && demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel > 0.0)
            demo->rgb[1] = demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel;

        if (demo->rgb[2] == -1 && demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel > 0.0)
            demo->rgb[2] = demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel;

        // Set maxL to, well, maxL. Set maxFALL to a weighted average of input rgb, because most of
        // the content is background color, and the 10% of Jesse are a bit "who cares?":
        setHdrMetadata(demo, maxL, 0.2126 * demo->rgb[0] + 0.7152 * demo->rgb[1] + 0.0722 * demo->rgb[2]);
    }

    // Background in user-specified R, G, B:
    glClearColor(demo->rgb[0], demo->rgb[1], demo->rgb[2], 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Map max intensity r, g, b in the cat picture to maxL nits:
    glColor3f(maxL, maxL, maxL);

    // Draw some rotating square into the center, textured with the cat pic:
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(demo->tx, demo->ty, 0.0);
    glRotatef((float)(demo->curFrame % 360), 0, 0, 1);
    glScalef(0.15, 0.15, 1);

    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2f(-1.0, -1.0);
    glTexCoord2f(1.0, 0.0);
    glVertex2f(1.0, -1.0);
    glTexCoord2f(1.0, 1.0);
    glVertex2f(1.0, 1.0);
    glTexCoord2f(0.0, 1.0);
    glVertex2f(-1.0, 1.0);
    glEnd();

    glDisable(GL_TEXTURE_2D);
}

void draw_centerpatch(struct demo* demo, bool flash, bool move)
{
    // Draw a RGB patch filling the center 10% of the display:
    static bool firsttime = true;

    float maxL = (demo->nativeDisplayHdrMetadata.maxLuminance > 0.0) ? demo->nativeDisplayHdrMetadata.maxLuminance : 600;

    if (firsttime) {
        firsttime = false;

        // Default color to maxLuminance:
        if (demo->rgb[0] == -1)
            demo->rgb[0] = maxL;

        if (demo->rgb[1] == -1)
            demo->rgb[1] = maxL;

        if (demo->rgb[2] == -1)
            demo->rgb[2] = maxL;

        // Set maxL to, well, maxL. Set maxFALL to a weighted average of input rgb * 0.1, because only 10% are non-black:
        setHdrMetadata(demo, maxL, (0.2126 * demo->rgb[0] + 0.7152 * demo->rgb[1] + 0.0722 * demo->rgb[2]) * 0.1);
    }

    // Background in black:
    glClearColor(0, 0, 0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // User defined RGB values in nits:
    if (!flash || ((demo->curFrame % 600) < 200))
        glColor3f(demo->rgb[0], demo->rgb[1], demo->rgb[2]);
    else
        glColor3f(0, 0, 0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (move) {
        float v = ((float)(demo->curFrame % 2000) / 1000.0) - 1.0;
        glTranslatef(sin(v * 3.1415) * 1.5, sin(v * 3.1415) * 1.5, 0.0);
    } else {
        glTranslatef(demo->tx, demo->ty, 0.0);
    }

    // Scale square to cover the center 10% of the display area:
    glScalef(0.31623, 0.31623, 1);

    glBegin(GL_QUADS);
    glVertex2f(-1.0, -1.0);
    glVertex2f(1.0, -1.0);
    glVertex2f(1.0, 1.0);
    glVertex2f(-1.0, 1.0);
    glEnd();
}

void draw_fullscreen(struct demo* demo, bool flash)
{
    // Draw a RGB color filling the whole display:
    static bool firsttime = true;

    float maxFALL = (demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel > 0.0) ? demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel : demo->nativeDisplayHdrMetadata.maxLuminance;
    if (maxFALL == 0)
        maxFALL = 600.0;

    if (firsttime) {
        firsttime = false;

        // Default background color to maxFALL:
        if (demo->rgb[0] == -1)
            demo->rgb[0] = maxFALL;

        if (demo->rgb[1] == -1)
            demo->rgb[1] = maxFALL;

        if (demo->rgb[2] == -1)
            demo->rgb[2] = maxFALL;

        maxFALL = (0.2126 * demo->rgb[0] + 0.7152 * demo->rgb[1] + 0.0722 * demo->rgb[2]);

        // Set maxL and maxFALL to a weighted average of input rgb:
        setHdrMetadata(demo, maxFALL, maxFALL);
    }

    if (!flash || ((demo->curFrame % 600) < 200)) {
        // Background in user specified color:
        glClearColor(demo->rgb[0], demo->rgb[1], demo->rgb[2], 1.0);
    }
    else {
        // Background black:
        glClearColor(0, 0, 0, 1);
    }

    glClear(GL_COLOR_BUFFER_BIT);
}

void draw_opengl_client(struct demo* demo)
{
    static bool firsttime = true;
    int w = demo->textures[0].tex_width;
    int h = demo->textures[0].tex_height;

    switch (demo->testpattern) {
        case 0:
        default:
            draw_jesse(demo);
            break;

        case 1:
            draw_centerpatch(demo, false, false);
            break;

        case 2:
            draw_centerpatch(demo, true, false);
            break;

        case 3:
            draw_centerpatch(demo, false, true);
            break;

        case 4:
            draw_fullscreen(demo, false);
            break;

        case 5:
            draw_fullscreen(demo, true);
            break;
    }
}

void draw_opengl(struct demo* demo)
{
    static bool firsttime = true;
    int w = demo->textures[0].tex_width;
    int h = demo->textures[0].tex_height;

    if (!demo->interop_enabled)
        return;

    if (firsttime) {
        glClampColor(GL_CLAMP_VERTEX_COLOR, GL_FALSE);
        glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);
        glViewport(0, 0, w, h);
        printf("Vulkan target fbo size: %i x %i\n", w, h);
        firsttime = false;
    }

    // Bind fbo with our virtual OpenGL framebuffer, so simulated client code
    // can render the stimulus image in RGBA16F nits, BT2020/2100 color space.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, demo->srcfbo);

    // Call simulated client rendering code:
    draw_opengl_client(demo);

    // Bind FBO with our Vulkan interop texture:
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, demo->dstfbo);

    if (true) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        //glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, demo->srctexture);
        //glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glUseProgram(demo->hdr_shader);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0);
        glVertex2f(-1.0, -1.0);
        glTexCoord2f(1.0, 0.0);
        glVertex2f(1.0, -1.0);
        glTexCoord2f(1.0, 1.0);
        glVertex2f(1.0, 1.0);
        glTexCoord2f(0.0, 1.0);
        glVertex2f(-1.0, 1.0);
        glEnd();
        glUseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        //glDisable(GL_TEXTURE_2D);
    }
    else {
        // Simple blit from src to dst, no OETF HDR shader applied:
        glBlitNamedFramebuffer(demo->srcfbo, demo->dstfbo, 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Poor man's sync until we use semaphores properly:
    glFinish();

    // Unbind, so Vulkan can texture / blit from it:
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

// hdrFragmentShaderSrc currently implements the ST-2084 PQ OETF, for EOTF
// decoding in the display. Iow. it implements the HDR-10 mapping from a
// linear color intensity input range (in nits aka cd/m2) from 0 - 10000 nits
// to the PQ mapped range 0.0 - 1.0, which then can get encoded into typically
// 10 bits per color channel and transmitted to the display for decoding.
static char hdrFragmentShaderSrc[] =
"uniform sampler2D Image; \n"
"\n"
"void main() \n"
"{ \n"
"   vec3 L, Lp, f, v; \n"
"\n"
"   /* Get source color sample */ \n"
"   vec4 uFragColor = texture2D(Image, gl_TexCoord[0].st); \n"
"\n"
"   /* Normalize input range [0 - 10000.0 nits] to [0.0 - 1.0]; */ \n"
"   L = uFragColor.rgb; \n"
"   L = L / 10000.0; \n"
"\n"
"   /* Apply ST 2084 PQ OETF */ \n"
"   Lp = pow(L, vec3(0.1593017578125)); \n"
"   f = (0.8359375 + 18.8515625 * Lp) / (1.0 + 18.6875 * Lp); \n"
"   v  = pow(f, vec3(78.84375)); \n"
"\n"
"   /* Debug range check: If red input value greater than some nits, color it red */ \n"
"   if (false && (uFragColor.r >= 1000.0)) \n"
"      v = vec3(1.0, 0.0, 0.0); \n"
"\n"
"   /* Assign PQ mapped to output */ \n"
"   gl_FragColor.a = uFragColor.a; \n"
"   gl_FragColor.rgb = v; \n"
"} \n";

GLuint PsychCreateGLSLProgram(const char* fragmentsrc, const char* vertexsrc)
{
    GLuint glsl = 0;
    GLuint shader;
    GLint status;
    char errtxt[10000];

    // Reset error state:
    while (glGetError());

    // Supported at all on this hardware?
    if (!glewIsSupported("GL_ARB_shader_objects") || !glewIsSupported("GL_ARB_shading_language_100")) {
        printf("PTB-ERROR: Your graphics hardware does not support GLSL fragment shaders! Use of imaging pipeline with current settings impossible!\n");
        return(0);
    }

    // Create GLSL program object:
    glsl = glCreateProgram();

    // Fragment shader wanted?
    if (fragmentsrc) {
        printf("PTB-INFO: Creating the following fragment shader, GLSL source code follows:\n\n%s\n\n", fragmentsrc);

        // Supported on this hardware?
        if (!glewIsSupported("GL_ARB_fragment_shader")) {
            printf("PTB-ERROR: Your graphics hardware does not support GLSL fragment shaders! Use of imaging pipeline with current settings impossible!\n");
            return(0);
        }

        // Create shader object:
        shader = glCreateShader(GL_FRAGMENT_SHADER);

        // Feed it with GLSL source code:
        glShaderSource(shader, 1, (const char**) &fragmentsrc, NULL);

        // Compile shader:
        glCompileShader(shader);

        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            printf("PTB-ERROR: Shader compilation for builtin fragment shader failed:\n");
            glGetShaderInfoLog(shader, 9999, NULL, (GLchar*) &errtxt);
            printf("%s\n\n", errtxt);

            glDeleteShader(shader);
            glDeleteProgram(glsl);

            // Failed!
            while (glGetError());

            return(0);
        }

        // Attach it to program object:
        glAttachShader(glsl, shader);
    }

    // Vertex shader wanted?
    if (vertexsrc) {
        printf("PTB-INFO: Creating the following vertex shader, GLSL source code follows:\n\n%s\n\n", vertexsrc);

        // Supported on this hardware?
        if (!glewIsSupported("GL_ARB_vertex_shader")) {
            printf("PTB-ERROR: Your graphics hardware does not support GLSL vertex shaders! Use of imaging pipeline with current settings impossible!\n");
            return(0);
        }

        // Create shader object:
        shader = glCreateShader(GL_VERTEX_SHADER);

        // Feed it with GLSL source code:
        glShaderSource(shader, 1, (const char**) &vertexsrc, NULL);

        // Compile shader:
        glCompileShader(shader);

        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            printf("PTB-ERROR: Shader compilation for builtin vertex shader failed:\n");
            glGetShaderInfoLog(shader, 9999, NULL, (GLchar*) &errtxt);
            printf("%s\n\n", errtxt);

            glDeleteShader(shader);
            glDeleteProgram(glsl);

            // Failed!
            while (glGetError());

            return(0);
        }

        // Attach it to program object:
        glAttachShader(glsl, shader);
    }

    // Link into final program object:
    glLinkProgram(glsl);

    // Check link status:
    glGetProgramiv(glsl, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        printf("PTB-ERROR: Shader link operation for builtin glsl program failed:\n");
        glGetProgramInfoLog(glsl, 9999, NULL, (GLchar*) &errtxt);
        printf("Error output follows:\n\n%s\n\n", errtxt);

        glDeleteProgram(glsl);

        // Failed!
        while (glGetError());

        return(0);
    }

    while (glGetError());

    // Return new GLSL program object handle:
    return(glsl);
}

static void demo_create_opengl_interop(struct demo* demo)
{
    GLint tilingMode;
    GLenum err;

    if (!demo->interop_enabled)
        return;

    while (glGetError());

    // Create the texture for the FBO color attachment.
    // This only reserves the ID, it doesn't allocate memory
    glCreateTextures(GL_TEXTURE_2D, 1, &demo->color);

    // Import semaphores
    glGenSemaphoresEXT(1, &demo->glReady);
    glGenSemaphoresEXT(1, &demo->glComplete);

    err = glGetError();
    if (err)
        printf("Stage 1: GL ERROR: %i\n", err);

#ifdef WIN32
    // Platform specific import.  On non-Win32 systems use glImportSemaphoreFdEXT instead
    //glImportSemaphoreWin32HandleEXT(demo->glReady, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, demo->interophandles.glReady);
    //glImportSemaphoreWin32HandleEXT(demo->glComplete, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, demo->interophandles.glComplete);
#else
    glImportSemaphoreFdEXT(demo->glReady, GL_HANDLE_TYPE_OPAQUE_FD_EXT, demo->interophandles.glReady);
    glImportSemaphoreFdEXT(demo->glComplete, GL_HANDLE_TYPE_OPAQUE_FD_EXT, demo->interophandles.glComplete);
#endif

    err = glGetError();
    if (err)
        printf("Stage 2: GL ERROR: %i\n", err);

    // Import memory
    glCreateMemoryObjectsEXT(1, &demo->mem);
#ifdef WIN32
    // Platform specific import.  On non-Win32 systems use glImportMemoryFdEXT instead
    glImportMemoryWin32HandleEXT(demo->mem, demo->textures[0].mem_alloc.allocationSize, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, demo->interophandles.memory);
#else
    glImportMemoryFdEXT(demo->mem, demo->textures[0].mem_alloc.allocationSize, GL_HANDLE_TYPE_OPAQUE_FD_EXT, demo->interophandles.memory);
#endif

    err = glGetError();
    if (err)
        printf("Stage 3: GL ERROR: %i\n", err);

    // Query actual tiling mode of texture:
    glBindTexture(GL_TEXTURE_2D, demo->color);

    // Use the imported memory as backing for the OpenGL texture.  The internalFormat, dimensions
    // and mip count should match the ones used by Vulkan to create the image and determine it's memory
    // allocation.
    GLenum internalFormat = (GLenum)0;
    switch (demo->interop_tex_format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
        internalFormat = GL_RGBA8;
        printf("OpenGL->Vulkan interop texture is format RGBA8\n");
        break;

    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        internalFormat = GL_RGB10_A2;
        printf("OpenGL->Vulkan interop texture is format RGB10A2\n");
        break;

    case VK_FORMAT_R16G16B16A16_SFLOAT:
        internalFormat = GL_RGBA16F;
        printf("OpenGL->Vulkan interop texture is format RGBA16F\n");
        break;

    default:
        printf("demo_create_opengl_interop: Invalid texture format!\n");
    }

    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, &tilingMode);
    if (tilingMode == GL_OPTIMAL_TILING_EXT)
        printf("Initially optimal tiling for shared texture.\n");
    else if (tilingMode == GL_LINEAR_TILING_EXT)
        printf("Initially linear tiling for shared texture.\n");
    else
        printf("Initially UNKNOWN tiling 0x%x for shared texture!\n", tilingMode);

    glGetInternalformativ(GL_TEXTURE_2D, internalFormat, GL_NUM_TILING_TYPES_EXT, 1, &tilingMode);
    printf("GL_NUM_TILING_TYPES_EXT %i\n", tilingMode);

    // Set tiling mode for rendering into textures:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, (demo->interop_tiled_texture) ? GL_OPTIMAL_TILING_EXT : GL_LINEAR_TILING_EXT);

    glTextureStorageMem2DEXT(demo->color, 1, internalFormat, demo->textures[0].tex_width, demo->textures[0].tex_height, demo->mem, 0);
    printf("Interop texture import size: %i x %i\n", demo->textures[0].tex_width, demo->textures[0].tex_height);
    err = glGetError();
    if (err)
        printf("Stage 4: GL ERROR: %i\n", err);

    // Query actual tiling mode of texture:
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, &tilingMode);
    if (tilingMode == GL_OPTIMAL_TILING_EXT)
        printf("Using optimal tiling for shared texture.\n");
    else if (tilingMode == GL_LINEAR_TILING_EXT)
        printf("Using linear tiling for shared texture.\n");
    else
        printf("Using UNKNOWN tiling 0x%x for shared texture!\n", tilingMode);

    err = glGetError();
    if (err)
        printf("Stage 5: GL ERROR: %i\n", err);

    // Create destination FBO, attach our imported/Vulkan-shared texture as color
    // buffer, so we can render-to-texture in OpenGL, present in Vulkan:
    glCreateFramebuffers(1, &demo->dstfbo);
    glNamedFramebufferTexture(demo->dstfbo, GL_COLOR_ATTACHMENT0, demo->color, 0);

    // Create our source FBO, into which our simulated OpenGL client renders.
    glCreateTextures(GL_TEXTURE_2D, 1, &demo->srctexture);
    glBindTexture(GL_TEXTURE_2D, demo->srctexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, demo->textures[0].tex_width, demo->textures[0].tex_height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glCreateFramebuffers(1, &demo->srcfbo);
    glNamedFramebufferTexture(demo->srcfbo, GL_COLOR_ATTACHMENT0, demo->srctexture, 0);

    // Build HDR post-processing shader:
    demo->hdr_shader = PsychCreateGLSLProgram(hdrFragmentShaderSrc, NULL);

    // Load image again, this time into the backing store of the GL_TEXTURE_2D
    // default binding 0 -- Yes, old school like it's 1992!
    {
        VkSubresourceLayout layout;
        uint8_t* data;
        int32_t width, height;
        const char* filename = tex_files[0];
        layout.rowPitch = 1024;

        if (!loadTexture(filename, NULL, NULL, &width, &height)) {
            fprintf(stderr, "Error probe-loading texture: %s\n", filename);
        }

        data = calloc(width * height * 4, sizeof(uint8_t));

        if (!loadTexture(filename, data, &layout, &width, &height)) {
            fprintf(stderr, "Error real-loading texture: %s\n", filename);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        free(data);
    }

    err = glGetError();
    if (err)
        printf("Stage 6: GL ERROR: %i\n", err);
}

// On MS-Windows, make this a global, so it's available to WndProc()
struct demo demo;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
static void demo_run(struct demo *demo) {
    if (!demo->prepared)
        return;

    demo_draw(demo);
    demo->curFrame++;
    if (demo->frameCount != INT_MAX && demo->curFrame == demo->frameCount) {
        PostQuitMessage(validation_error);
    }
}

// MS-Windows event handling function:
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CHAR:
        if (wParam != 'q')
            break;
        // fallthrough to WM_CLOSE if q key pressed:
    case WM_CLOSE:
        PostQuitMessage(validation_error);
        break;
    case WM_PAINT:
        // The validation callback calls MessageBox which can generate paint
        // events - don't make more Vulkan calls if we got here from the
        // callback
        if (!in_callback) {
            demo_run(&demo);
        }
        break;
    case WM_GETMINMAXINFO:     // set window's minimum size
        ((MINMAXINFO*)lParam)->ptMinTrackSize = demo.minsize;
        return 0;
    case WM_SIZE:
        // Resize the application to the new window size, except when
        // it was minimized. Vulkan doesn't support images or swapchains
        // with width=0 and height=0.
        if (wParam != SIZE_MINIMIZED) {
            demo.width = lParam & 0xffff;
            demo.height = (lParam & 0xffff0000) >> 16;
            demo_resize(&demo);
        }
        break;
    default:
        break;
    }
    return (DefWindowProc(hWnd, uMsg, wParam, lParam));
}

static void demo_create_window(struct demo *demo) {
    WNDCLASSEX win_class;
    int pf;
    GLenum glerr;
    HWND glwindow;
    HDC hDC;
    HGLRC glcontext;
    PIXELFORMATDESCRIPTOR pfd;

    // Initialize the window class structure:
    win_class.cbSize = sizeof(WNDCLASSEX);
    win_class.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    win_class.lpfnWndProc = WndProc;
    win_class.cbClsExtra = 0;
    win_class.cbWndExtra = 0;
    win_class.hInstance = demo->connection; // hInstance
    win_class.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    win_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    win_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    win_class.lpszMenuName = NULL;
    win_class.lpszClassName = demo->name;
    win_class.hIconSm = LoadIcon(NULL, IDI_WINLOGO);
    // Register window class:
    if (!RegisterClassEx(&win_class)) {
        // It didn't work, so try to give a useful error:
        printf("Unexpected error trying to start the application!\n");
        fflush(stdout);
        exit(1);
    }

    // Create window with the registered class:
    //RECT wr = {0, 0, GetSystemMetrics(SM_CXFULLSCREEN), GetSystemMetrics(SM_CYFULLSCREEN)};
    //AdjustWindowRectEx(&wr, WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, WS_EX_TOPMOST | WS_EX_APPWINDOW);
    demo->window = CreateWindowEx(WS_EX_TOPMOST | WS_EX_APPWINDOW,
                                  demo->name,           // class name
                                  demo->name,           // app name
                                  WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, // window style | WS_SYSMENU,
                                  0, 0,  // x/y coords
                                  GetSystemMetrics(SM_CXSCREEN), // width
                                  GetSystemMetrics(SM_CYSCREEN), // height
                                  NULL,               // handle to parent
                                  NULL,               // handle to menu
                                  demo->connection,   // hInstance
                                  NULL);              // no extra parameters
    if (!demo->window) {
        // It didn't work, so try to give a useful error:
        printf("Cannot create a window in which to draw!\n");
        fflush(stdout);
        exit(1);
    }

    // Window client area size must be at least 1 pixel high, to prevent crash.
    demo->minsize.x = GetSystemMetrics(SM_CXSCREEN); // GetSystemMetrics(SM_CXMINTRACK);
    demo->minsize.y = GetSystemMetrics(SM_CYSCREEN); // GetSystemMetrics(SM_CYMINTRACK) + 1;

    glwindow = CreateWindowEx(0,
                              demo->name,           // class name
                              demo->name,           // app name
                              WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, // window style
                              0, 0,  // x/y coords
                              100, // width
                              100, // height
                              NULL, // handle to parent
                              NULL, // handle to menu
                              demo->connection, // hInstance
                              NULL); // no extra parameters

    if (!glwindow) {
        // It didn't work, so try to give a useful error:
        printf("Cannot create a OpenGL dummy window!\n");
        fflush(stdout);
        exit(1);
    }

    hDC = GetDC(glwindow);

    // Build pixelformat descriptor:
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_SWAP_EXCHANGE | PFD_DOUBLEBUFFER;  // Want OpenGL capable window with bufferswap via page-flipping...
    pfd.iPixelType = PFD_TYPE_RGBA; // Want a RGBA pixel format.
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8; // Usually want an at least 8 bit alpha-buffer, unless high color bit depths formats requested.
    pf = ChoosePixelFormat(hDC, &pfd);

    // Do we have a valid pixelformat?
    if (pf == 0) {
        // Nope. We give up!
        printf("\nChoosePixelFormat() failed: Unknown error, Win32 specific.\n\n");
        exit(1);
    }

    // Yes. Set it:
    if (SetPixelFormat(hDC, pf, &pfd) == FALSE) {
        printf("\nSetPixelFormat() failed: Unknown error, Win32 specific.\n\n");
        exit(1);
    }

    glcontext = wglCreateContext(hDC);
    if (glcontext == NULL) {
        printf("\nOpenGL context creation failed: Unknown, Win32 specific.\n\n");
        exit(1);
    }

    wglMakeCurrent(hDC, glcontext);

    // Ok, the OpenGL rendering context is up and running. Auto-detect and bind all
    // available OpenGL extensions via GLEW:
    glerr = glewInit();
    if (GLEW_OK != glerr) {
        /* Problem: glewInit failed, something is seriously wrong. */
        printf("\nGLEW init failed: %s: Will try to continue, but may crash soon!\n\n", glewGetErrorString(glerr));
        fflush(NULL);
        exit(1);
    }
}

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
static void demo_create_xlib_window(struct demo *demo) {

    XInitThreads();
    demo->display = XOpenDisplay(NULL);
    long visualMask = VisualScreenMask;
    int numberOfVisuals;
    XVisualInfo vInfoTemplate={};
    vInfoTemplate.screen = DefaultScreen(demo->display);
    XVisualInfo *visualInfo = XGetVisualInfo(demo->display, visualMask,
                                             &vInfoTemplate, &numberOfVisuals);

    Colormap colormap = XCreateColormap(
                demo->display, RootWindow(demo->display, vInfoTemplate.screen),
                visualInfo->visual, AllocNone);

    XSetWindowAttributes windowAttributes={};
    windowAttributes.colormap = colormap;
    windowAttributes.background_pixel = 0xFFFFFFFF;
    windowAttributes.border_pixel = 0;
    windowAttributes.event_mask =
            KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;

    demo->xlib_window = XCreateWindow(
                demo->display, RootWindow(demo->display, vInfoTemplate.screen), 0, 0,
                demo->width, demo->height, 0, visualInfo->depth, InputOutput,
                visualInfo->visual,
                CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &windowAttributes);

    XSelectInput(demo->display, demo->xlib_window, ExposureMask | KeyPressMask);
    XMapWindow(demo->display, demo->xlib_window);
    XFlush(demo->display);
    demo->xlib_wm_delete_window =
            XInternAtom(demo->display, "WM_DELETE_WINDOW", False);
}
static void demo_handle_xlib_event(struct demo *demo, const XEvent *event) {
    switch(event->type) {
    case ClientMessage:
        if ((Atom)event->xclient.data.l[0] == demo->xlib_wm_delete_window)
            demo->quit = true;
        break;
    case KeyPress:
        switch (event->xkey.keycode) {
        case 0x9: // Escape
            demo->quit = true;
            break;
        case 0x71: // left arrow key
            demo->spin_angle -= demo->spin_increment;
            break;
        case 0x72: // right arrow key
            demo->spin_angle += demo->spin_increment;
            break;
        case 0x41: // space bar
            demo->pause = !demo->pause;
            break;
        }
        break;
    case ConfigureNotify:
        if ((demo->width != event->xconfigure.width) ||
            (demo->height != event->xconfigure.height)) {
            demo->width = event->xconfigure.width;
            demo->height = event->xconfigure.height;
            demo_resize(demo);
        }
        break;
    default:
        break;
    }

}

static void demo_run_xlib(struct demo *demo) {

    while (!demo->quit) {
        XEvent event;

        if (demo->pause) {
            XNextEvent(demo->display, &event);
            demo_handle_xlib_event(demo, &event);
        }
        while (XPending(demo->display) > 0) {
            XNextEvent(demo->display, &event);
            demo_handle_xlib_event(demo, &event);
        }

        demo_draw(demo);
        demo->curFrame++;
        if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
            demo->quit = true;
    }
}
#elif defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_DISPLAY_KHR)

static void demo_handle_xcb_event(struct demo *demo,
                              const xcb_generic_event_t *event) {
    uint8_t event_code = event->response_type & 0x7f;
    switch (event_code) {
    case XCB_EXPOSE:
        // TODO: Resize window
        printf("XCB_EXPOSE\n");

        break;
    case XCB_CLIENT_MESSAGE:
        if ((*(xcb_client_message_event_t *)event).data.data32[0] ==
            (*demo->atom_wm_delete_window).atom) {
            demo->quit = true;
        }
        printf("XCB_CLIENT_MESSAGE\n");

        break;
    case XCB_KEY_RELEASE: {
        const xcb_key_release_event_t *key =
            (const xcb_key_release_event_t *)event;

        switch (key->detail) {
        case 0x9: // Escape
    case 0x18: // q key
            demo->quit = true;
            break;
        case 0x71: // left arrow key
            demo->spin_angle -= demo->spin_increment;
            break;
        case 0x72: // right arrow key
            demo->spin_angle += demo->spin_increment;
            break;
        case 0x41: // space bar
            demo->pause = !demo->pause;
            break;
        }
        printf("KEY %x\n", key->detail);
    } break;
    case XCB_CONFIGURE_NOTIFY: {
        const xcb_configure_notify_event_t *cfg =
            (const xcb_configure_notify_event_t *)event;
            printf("XCB_CONFIGURE_NOTIFY\n");

/*
        if ((demo->width != cfg->width) || (demo->height != cfg->height)) {
            demo->width = cfg->width;
            demo->height = cfg->height;
            demo_resize(demo);
        }
*/
    } break;
    default:
        break;
    }
}

static void demo_run_xcb(struct demo *demo) {
    unsigned char keys_return[32];
    unsigned int keysdown, i, j;

    xcb_flush(demo->connection);

    while (!demo->quit) {
        xcb_generic_event_t *event;

        if (demo->pause) {
            event = xcb_wait_for_event(demo->connection);
        }
        else {
            event = xcb_poll_for_event(demo->connection);
        }
        while (event) {
            demo_handle_xcb_event(demo, event);
            free(event);
            event = xcb_poll_for_event(demo->connection);
        }

        memset(keys_return, 0, sizeof(keys_return));
        keysdown = 0;

        if (demo->display)
            XQueryKeymap(demo->display, (char*) keys_return);

        for (i = 0; i < 32; i++) keysdown+=(unsigned int) keys_return[i];

        // Map 32 times 8 bitvector to 256 element return vector:
        for(i = 0; i < 32 && !keysdown; i++) {
            for(j = 0; j < 8; j++) {
                // This button or key down?
                if (keys_return[i] & (1<<j)) {
                    keysdown = i * 8 + j;
                    break;
                }
            }
        }

        if (keysdown)
            printf("keysdown %i\n", keysdown);

        switch (keysdown) {
            case 0x1: // q key
                demo->quit = true;
                break;
            case 0x2: // left arrow key
                demo->spin_angle -= demo->spin_increment;
                break;
            case 0x4: // right arrow key
                demo->spin_angle += demo->spin_increment;
                break;
            case 0x41: // space bar
                demo->pause = !demo->pause;
                break;
        }

        demo_draw(demo);
        demo->curFrame++;
        if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
            demo->quit = true;
    }
}

static void demo_create_xcb_window(struct demo *demo) {
    uint32_t value_mask, value_list[32];
    uint32_t override_redirect = 1;

    demo->xcb_window = xcb_generate_id(demo->connection);

    value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    value_list[0] = demo->screen->black_pixel;
    value_list[1] = XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE |
                    XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    demo->visualID = demo->screen->root_visual;

    // MK TODO: Adjust x start position to viewport of output which displays Vulkan,
    // instead of hard-coded 300 for the internal output:
    xcb_create_window(demo->connection, XCB_COPY_FROM_PARENT, demo->xcb_window,
                      demo->screen->root, 300, 0, demo->width, demo->height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, /*demo->screen->root_visual*/ demo->visualID,
                      value_mask, value_list);

    /* Magic code that will send notification when window is destroyed */
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(demo->connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(demo->connection, cookie, 0);

    xcb_intern_atom_cookie_t cookie2 =
        xcb_intern_atom(demo->connection, 0, 16, "WM_DELETE_WINDOW");
    demo->atom_wm_delete_window =
        xcb_intern_atom_reply(demo->connection, cookie2, 0);

    xcb_change_property(demo->connection, XCB_PROP_MODE_REPLACE, demo->xcb_window,
                        (*reply).atom, 4, 32, 1,
                        &(*demo->atom_wm_delete_window).atom);
    free(reply);

    // MK Need override_redirect so WM does leave our window on the target display output for Vulkan:
    xcb_change_window_attributes(demo->connection, demo->xcb_window, XCB_CW_OVERRIDE_REDIRECT, &override_redirect);

    // MK this is not enough though. We need to set the X-Screens primary output to
    // the Vulkan output, so the DDX selects the right crtc, given that it ignores
    // all crtc's with disconnected outputs, and therefore the normal "max viewport <-> window intersection area"
    // method does not work, and the primary output is always chosen for (msc,ust) queries.
    //
    // Well that, or hack the DDX to "consider_disabled" crtc's during crtc picking
    // DRI3/Present...

    xcb_map_window(demo->connection, demo->xcb_window);
    xcb_flush(demo->connection);
}

/*
 *        Attribs filter the list of FBConfigs returned by glXChooseFBConfig().
 *        Visual attribs further described in glXGetFBConfigAttrib(3)
 */
static int visual_attribs[] =
{
    GLX_X_RENDERABLE, True,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    GLX_STENCIL_SIZE, 8,
    GLX_DOUBLEBUFFER, True,
    //GLX_SAMPLE_BUFFERS  , 1,
    //GLX_SAMPLES         , 4,
    None
};

static void demo_create_glx_opengl1(struct demo *demo)
{
    int visualID = 0;

    /* Query framebuffer configurations that match visual_attribs */
    GLXFBConfig *fb_configs = 0;
    int num_fb_configs = 0;

    fb_configs = glXChooseFBConfig(demo->display, DefaultScreen(demo->display), visual_attribs, &num_fb_configs);
    if(!fb_configs || num_fb_configs == 0)
    {
        fprintf(stderr, "glXGetFBConfigs failed\n");
        exit(1);
    }

    printf("Found %d matching FB configs\n", num_fb_configs);

    /* Select first framebuffer config and query visualID */
    GLXFBConfig fb_config = fb_configs[0];
    glXGetFBConfigAttrib(demo->display, fb_config, GLX_VISUAL_ID , &visualID);

    demo->visualID = visualID;

    /* Create OpenGL context */
    demo->context = glXCreateNewContext(demo->display, fb_config, GLX_RGBA_TYPE, 0, True);
    if(!demo->context)
    {
        fprintf(stderr, "glXCreateNewContext failed\n");
        exit(1);
    }

    demo->fb_config = fb_config;
}

static void demo_create_glx_opengl2(struct demo *demo)
{
    /* Create GLX Window */
    GLXDrawable drawable = 0;

    GLXWindow glxwindow = glXCreateWindow(
        demo->display,
        demo->fb_config,
        demo->xcb_window,
        0);

    if(!glxwindow)
    {
        xcb_destroy_window(demo->connection, demo->xcb_window);
        glXDestroyContext(demo->display, demo->context);

        fprintf(stderr, "glXDestroyContext failed\n");
        exit(1);
    }

    drawable = glxwindow;

    /* make OpenGL context current */
    if(!glXMakeContextCurrent(demo->display, drawable, drawable, demo->context))
    {
        xcb_destroy_window(demo->connection, demo->xcb_window);
        glXDestroyContext(demo->display, demo->context);

        fprintf(stderr, "glXMakeContextCurrent failed\n");
        exit(1);
    }
    demo->drawable = drawable;

    GLenum glerr = glewInit();
    if (glerr != GLEW_OK) {
        printf("glewInit failed!\n");
        exit(1);
    }
    printf("\nUsing GLEW version %s for OpenGL.\n", glewGetString(GLEW_VERSION));
    printf("OpenGL renderer: %s %s - OpenGL %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

    glXGetSyncValuesOML = (PFNGLXGETSYNCVALUESOMLPROC) glXGetProcAddress("glXGetSyncValuesOML");
}

// VK_USE_PLATFORM_XCB_KHR
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
static void demo_run(struct demo *demo) {
    while (!demo->quit) {
        demo_draw(demo);
        demo->curFrame++;
        if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount)
            demo->quit = true;
    }
}

static void handle_ping(void *data UNUSED,
                        struct wl_shell_surface *shell_surface,
                        uint32_t serial) {
    wl_shell_surface_pong(shell_surface, serial);
}

static void handle_configure(void *data UNUSED,
                             struct wl_shell_surface *shell_surface UNUSED,
                             uint32_t edges UNUSED, int32_t width UNUSED,
                             int32_t height UNUSED) {}

static void handle_popup_done(void *data UNUSED,
                              struct wl_shell_surface *shell_surface UNUSED) {}

static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping, handle_configure, handle_popup_done};

static void demo_create_window(struct demo *demo) {
    demo->window = wl_compositor_create_surface(demo->compositor);
    if (!demo->window) {
        printf("Can not create wayland_surface from compositor!\n");
        fflush(stdout);
        exit(1);
    }

    demo->shell_surface = wl_shell_get_shell_surface(demo->shell, demo->window);
    if (!demo->shell_surface) {
        printf("Can not get shell_surface from wayland_surface!\n");
        fflush(stdout);
        exit(1);
    }
    wl_shell_surface_add_listener(demo->shell_surface, &shell_surface_listener,
                                  demo);
    wl_shell_surface_set_toplevel(demo->shell_surface);
    wl_shell_surface_set_title(demo->shell_surface, APP_SHORT_NAME);
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
static void demo_run(struct demo *demo) {
    if (!demo->prepared)
        return;

    demo_draw(demo);
    demo->curFrame++;
}
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#endif

// MK
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
// #elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
// Forward define prototype of helper function that is defined after us:
static VkBool32 get_x_lease(struct demo *demo, VkDisplayKHR khr_display);

static VkResult demo_create_display_surface(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;
    uint32_t display_count;
    uint32_t mode_count;
    uint32_t plane_count;
    VkDisplayPropertiesKHR *display_props;
    VkDisplayKHR display;
    VkDisplayModePropertiesKHR mode_props[100];
    VkDisplayPlanePropertiesKHR *plane_props;
    VkBool32 found_plane = VK_FALSE;
    uint32_t plane_index;
    VkExtent2D image_extent;
    VkDisplaySurfaceCreateInfoKHR create_info;

    // Do we already have an output leased from X11?
    if (!demo->vkdisplay) {
        // Nope: Enumerate available displays:
        err = vkGetPhysicalDeviceDisplayPropertiesKHR(demo->gpu, &display_count, NULL);
        assert(!err);

        if (display_count == 0) {
            printf("Cannot find any display!\n");
            fflush(stdout);
            exit(1);
        }

        display_props = calloc(display_count, sizeof (VkDisplayPropertiesKHR));
        err = vkGetPhysicalDeviceDisplayPropertiesKHR(demo->gpu, &display_count, display_props);
        assert(!err || (err == VK_INCOMPLETE));

        // Get last display:
        demo->vkdisplay = display_props[display_count-1].display;

        if (!demo->connection) {
            // Nope. Maybe running directly on VT without X-Server? Try to get a
            // direct DRM/KMS display, ie. become DRM-Master:
            printf("Apparently running directly on Linux DRM framebuffer in a VT. Taking over as DRM master.\n");
        }
        else {
            printf("Apparently first attempt at leasing the display failed due to a Vulkan driver bug?! Retrying differently.\n");
            if (!get_x_lease(demo, demo->vkdisplay)) {
                printf("Leasing the display failed again! Giving up.\n");
                fflush(stdout);
                exit(1);
            }
        }
    }

    display = demo->vkdisplay;

    // Get the first mode of the display
    err = vkGetDisplayModePropertiesKHR(demo->gpu, display, &mode_count, NULL);
    assert(!err);

    if (mode_count == 0) {
        printf("Cannot find any mode for the display!\n");
        fflush(stdout);
        exit(1);
    }

    printf("%d modes\n", mode_count);

    mode_count = (mode_count > 100) ? 100 : mode_count;
    err = vkGetDisplayModePropertiesKHR(demo->gpu, display, &mode_count, &mode_props[0]);
    assert(!err || (err == VK_INCOMPLETE));

    for (int i = 0; i < mode_count; i++) {
        printf("Mode[%i]: %d x %d @%f Hz\n", i, mode_props[i].parameters.visibleRegion.width, mode_props[i].parameters.visibleRegion.height, (float) mode_props[i].parameters.refreshRate / 1000.0f);
        if (mode_props[i].parameters.visibleRegion.width <= demo->max_width &&
            mode_props[i].parameters.visibleRegion.height <= demo->max_height &&
            mode_props[i].parameters.refreshRate >= demo->min_hz * 1000) {
            mode_props[0] = mode_props[i];
            break;
        }
    }

    // Get the list of planes
    err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(demo->gpu, &plane_count, NULL);
    assert(!err);

    if (plane_count == 0) {
        printf("Cannot find any plane!\n");
        fflush(stdout);
        exit(1);
    }

    plane_props = malloc(sizeof(VkDisplayPlanePropertiesKHR) * plane_count);
    assert(plane_props);

    err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(demo->gpu, &plane_count, plane_props);
    assert(!err);

    // Find a plane compatible with the display
    for (plane_index = 0; plane_index < plane_count; plane_index++) {
        uint32_t supported_count;
        VkDisplayKHR *supported_displays;

        // Disqualify planes that are bound to a different display
        if ((plane_props[plane_index].currentDisplay != VK_NULL_HANDLE) &&
            (plane_props[plane_index].currentDisplay != display)) {
            continue;
        }

        err = vkGetDisplayPlaneSupportedDisplaysKHR(demo->gpu, plane_index, &supported_count, NULL);
        assert(!err);

        if (supported_count == 0) {
            continue;
        }

        supported_displays = malloc(sizeof(VkDisplayKHR) * supported_count);
        assert(supported_displays);

        err = vkGetDisplayPlaneSupportedDisplaysKHR(demo->gpu, plane_index, &supported_count, supported_displays);
        assert(!err);

        printf("%d supported displays\n", supported_count);

        for (uint32_t i = 0; i < supported_count; i++) {
            if (supported_displays[i] == display) {
                found_plane = VK_TRUE;
                break;
            }
        }

        free(supported_displays);

        if (found_plane) {
            break;
        }
    }

    if (!found_plane) {
        printf("Cannot find a plane compatible with the display!\n");
        fflush(stdout);
        exit(1);
    }

    free(plane_props);

    VkDisplayPlaneCapabilitiesKHR planeCaps;
    vkGetDisplayPlaneCapabilitiesKHR(demo->gpu, mode_props[0].displayMode, plane_index, &planeCaps);
    // Find a supported alpha mode
    VkCompositeAlphaFlagBitsKHR alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR alphaModes[4] = {
        VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR,
        VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR,
    };
    for (uint32_t i = 0; i < sizeof(alphaModes); i++) {
        if (planeCaps.supportedAlpha & alphaModes[i]) {
            alphaMode = alphaModes[i];
            break;
        }
    }
    image_extent.width = mode_props[0].parameters.visibleRegion.width;
    image_extent.height = mode_props[0].parameters.visibleRegion.height;

    create_info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = NULL;
    create_info.flags = 0;
    create_info.displayMode = mode_props[0].displayMode;
    create_info.planeIndex = plane_index;
    create_info.planeStackIndex = plane_props[plane_index].currentStackIndex;
    create_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    create_info.alphaMode = alphaMode;
    create_info.globalAlpha = 1.0f;
    create_info.imageExtent = image_extent;

    return vkCreateDisplayPlaneSurfaceKHR(demo->inst, &create_info, NULL, &demo->surface);
}

#endif

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
static void demo_run_display(struct demo *demo)
{
    while (!demo->quit) {
        demo_draw(demo);
        demo->curFrame++;

        if (demo->frameCount != INT32_MAX && demo->curFrame == demo->frameCount) {
            demo->quit = true;
        }
    }
}
#endif

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
static VkBool32 demo_check_layers(uint32_t check_count, char **check_names,
                                  uint32_t layer_count,
                                  VkLayerProperties *layers) {
    for (uint32_t i = 0; i < check_count; i++) {
        VkBool32 found = 0;
        for (uint32_t j = 0; j < layer_count; j++) {
            if (!strcmp(check_names[i], layers[j].layerName)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Cannot find layer: %s\n", check_names[i]);
            return 0;
        }
    }
    return 1;
}

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)

static VkBool32 get_x_lease(struct demo *demo, VkDisplayKHR khr_display)
{
    xcb_connection_t *connection;
    int screen = 0;
    int fd;
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    Display *dpy;
    int scr;
    xcb_randr_output_t output = 0;
    VkResult err;

    // First pass, no X-Display connection yet?
    if (!demo->display) {
        // Try to get one:
        dpy = XOpenDisplay(NULL);

        // None? We are not running under a X11 X-Server, but on the VT directly,
        // no need for x-leasing, just no-op return with success:
        if (!dpy)
            return true;

        // Setup all the XCB stuff, screens, etc. find suitable RandR output
        // to lease and display on:
        scr = DefaultScreen(dpy);
        screen = scr;
        printf("Using X-Screen %i\n", screen);

        demo->connection = XGetXCBConnection(dpy);

        if (xcb_connection_has_error(demo->connection) > 0) {
            printf("Cannot find a compatible Vulkan installable client driver "
                "(ICD).\nExiting ...\n");
            fflush(stdout);
            exit(1);
        }

        setup = xcb_get_setup(demo->connection);
        iter = xcb_setup_roots_iterator(setup);
        while (scr-- > 0)
            xcb_screen_next(&iter);

        demo->screen = iter.data;

        connection = demo->connection;

        xcb_randr_query_version_cookie_t rqv_c = xcb_randr_query_version(connection,
                                                                        XCB_RANDR_MAJOR_VERSION,
                                                                        XCB_RANDR_MINOR_VERSION);
        xcb_randr_query_version_reply_t *rqv_r = xcb_randr_query_version_reply(connection, rqv_c, NULL);

        if (!rqv_r || rqv_r->minor_version < 6) {
            printf("No new-enough RandR version. Need RandR 1.6+\n");
            return 0;
        }

        xcb_screen_iterator_t s_i;

        int i_s = 0;

        for (s_i = xcb_setup_roots_iterator(xcb_get_setup(connection));
            s_i.rem;
            xcb_screen_next(&s_i), i_s++) {
            //printf ("index %d screen %d\n", s_i.index, screen);
            if (i_s == screen)
                break;
        }

        xcb_window_t root = s_i.data->root;

        //printf("root window id x%x\n", root);

        xcb_randr_get_screen_resources_cookie_t gsr_c = xcb_randr_get_screen_resources(connection, root);

        xcb_randr_get_screen_resources_reply_t *gsr_r = xcb_randr_get_screen_resources_reply(connection, gsr_c, NULL);

        if (!gsr_r) {
            printf("get_screen_resources failed\n");
            return 0;
        }

        xcb_randr_output_t *ro = xcb_randr_get_screen_resources_outputs(gsr_r);
        int o, c;

        /* Find a connected but idle output */
        output = 0;
        for (o = 0; output == 0 && o < gsr_r->num_outputs; o++) {
            xcb_randr_get_output_info_cookie_t goi_c = xcb_randr_get_output_info(connection, ro[o], gsr_r->config_timestamp);

            xcb_randr_get_output_info_reply_t *goi_r = xcb_randr_get_output_info_reply(connection, goi_c, NULL);

            /* Find the first connected but unused output */
            if (goi_r->connection == XCB_RANDR_CONNECTION_CONNECTED) {
                printf("Found connected output %s.\n", xcb_randr_get_output_info_name(goi_r));
                if (!demo->output_name[0] || strstr(xcb_randr_get_output_info_name(goi_r), demo->output_name)) {
                    output = ro[o];
                    printf("Selected output %s.\n", xcb_randr_get_output_info_name(goi_r));
                }
            }

            free(goi_r);
        }

        demo->display = dpy;
    }

    // Running under an X-Server, and have X11 / XCB connections, screens etc.

    // Get Extensions we need:
    PFN_vkGetRandROutputDisplayEXT     m_pGetRandROutputDisplayEXT = (PFN_vkGetRandROutputDisplayEXT) vkGetInstanceProcAddr(demo->inst, "vkGetRandROutputDisplayEXT" );
    PFN_vkAcquireXlibDisplayEXT        m_pAcquireXlibDisplayEXT = (PFN_vkAcquireXlibDisplayEXT) vkGetInstanceProcAddr(demo->inst, "vkAcquireXlibDisplayEXT" );

    // Already leased?
    if (demo->leasedAlready)
        return true;

    // Is this the first pass, where we try to find the khr_display to lease via
    // Vulkan RandR extension?
    if (khr_display == NULL) {
        // Yep: Map RandR output to khr_display, hopefully:
        //printf("Using output 0x%x\n", output);

        #if defined(VK_USE_PLATFORM_DISPLAY_KHR)
        demo_create_glx_opengl1(demo);
        demo_create_xcb_window(demo);
        #endif

        err = m_pGetRandROutputDisplayEXT(demo->gpu,
                                        demo->display,
                                        output,
                                        &khr_display);

        printf("err = %i\n", err);
        assert (!err);
        printf("Mapped to khr_display %p\n", khr_display);

        // Success?
        if (khr_display == NULL) {
            // Nope. Need to retry in 2nd pass:
            printf("Failed to find khr_display for RandR output. Bailing for now, retrying later...\n");
            return(false);
        }

        // Yes. Try to lease it.
    }
    else {
        // Nope. 2nd pass, just try to lease whatever was passed in as khr_display
        // from our caller:
        printf("Using khr_display %p passed in from caller in 2nd pass.\n", khr_display);
    }

    printf("Trying to acquire / lease khr_display %p, using gpu %p, X-Display %p\n", khr_display, demo->gpu, demo->display);
    err = m_pAcquireXlibDisplayEXT(demo->gpu, demo->display, khr_display);
    printf("err = %i\n", err);

    //assert (!err);
    if (err) {
        printf("Leasing %p FAILED!\n", khr_display);
        return false;
    }

    printf("Successfully leased %p !\n", khr_display);
    demo->leasedAlready = true;
    demo->vkdisplay = khr_display;

    return true;
}
#endif

static void demo_init_vk(struct demo *demo) {
    VkResult err;
    uint32_t instance_extension_count = 0;
    uint32_t instance_layer_count = 0;
    uint32_t validation_layer_count = 0;
    char **instance_validation_layers = NULL;
    demo->enabled_extension_count = 0;
    demo->enabled_layer_count = 0;

    char *instance_validation_layers_alt1[] = {
        "VK_LAYER_LUNARG_standard_validation"
    };

    char *instance_validation_layers_alt2[] = {
        "VK_LAYER_GOOGLE_threading",      "VK_LAYER_LUNARG_parameter_validation",
        "VK_LAYER_LUNARG_object_tracker", "VK_LAYER_LUNARG_core_validation",
        "VK_LAYER_LUNARG_swapchain",      "VK_LAYER_GOOGLE_unique_objects"
    };

    /* Look for validation layers */
    VkBool32 validation_found = 0;
    if (demo->validate) {

        err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
        assert(!err);

        instance_validation_layers = instance_validation_layers_alt1;
        if (instance_layer_count > 0) {
            VkLayerProperties *instance_layers =
                    malloc(sizeof (VkLayerProperties) * instance_layer_count);
            err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                    instance_layers);
            assert(!err);


            validation_found = demo_check_layers(
                    ARRAY_SIZE(instance_validation_layers_alt1),
                    instance_validation_layers, instance_layer_count,
                    instance_layers);
            if (validation_found) {
                demo->enabled_layer_count = ARRAY_SIZE(instance_validation_layers_alt1);
                demo->enabled_layers[0] = "VK_LAYER_LUNARG_standard_validation";
                validation_layer_count = 1;
            } else {
                // use alternative set of validation layers
                instance_validation_layers = instance_validation_layers_alt2;
                demo->enabled_layer_count = ARRAY_SIZE(instance_validation_layers_alt2);
                validation_found = demo_check_layers(
                    ARRAY_SIZE(instance_validation_layers_alt2),
                    instance_validation_layers, instance_layer_count,
                    instance_layers);
                validation_layer_count =
                    ARRAY_SIZE(instance_validation_layers_alt2);
                for (uint32_t i = 0; i < validation_layer_count; i++) {
                    demo->enabled_layers[i] = instance_validation_layers[i];
                }
            }
            free(instance_layers);
        }

        if (!validation_found) {
            ERR_EXIT("vkEnumerateInstanceLayerProperties failed to find "
                    "required validation layer.\n\n"
                    "Please look at the Getting Started guide for additional "
                    "information.\n",
                    "vkCreateInstance Failure");
        }
    }

    /* Look for instance extensions */
    VkBool32 surfaceExtFound = 0;
    VkBool32 platformSurfaceExtFound = 0;
    VkBool32 kmsExtFound = 0;
    VkBool32 displayExtFound = 0;
    unsigned int interopExtsFound = 0;
    unsigned int fullscreenInstanceExtsFound = 0;
    unsigned int hdrInstanceExtsFound = 0;
    memset(demo->extension_names, 0, sizeof(demo->extension_names));

    err = vkEnumerateInstanceExtensionProperties(
        NULL, &instance_extension_count, NULL);
    assert(!err);

    if (instance_extension_count > 0) {
        VkExtensionProperties *instance_extensions =
            malloc(sizeof(VkExtensionProperties) * instance_extension_count);
        err = vkEnumerateInstanceExtensionProperties(
            NULL, &instance_extension_count, instance_extensions);
        assert(!err);
        for (uint32_t i = 0; i < instance_extension_count; i++) {
            if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                surfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_SURFACE_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME;
            }

#if defined(VK_USE_PLATFORM_WIN32_KHR)
            if (!strcmp(VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
            }

#elif defined(VK_USE_PLATFORM_XLIB_KHR)
            if (!strcmp(VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_DISPLAY_KHR)
            if (!strcmp(VK_KHR_XCB_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_XCB_SURFACE_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
            if (!strcmp(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
            if (!strcmp(VK_KHR_DISPLAY_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                displayExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_DISPLAY_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
            if (!strcmp(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
                        instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] =
                    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_IOS_MVK)
            if (!strcmp(VK_MVK_IOS_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_MVK_IOS_SURFACE_EXTENSION_NAME;
            }
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
            if (!strcmp(VK_MVK_MACOS_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
                platformSurfaceExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_MVK_MACOS_SURFACE_EXTENSION_NAME;
            }
#endif

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
            if (!strcmp(VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
            instance_extensions[i].extensionName)) {
                fullscreenInstanceExtsFound++;
                printf("found acquire xlib display extension\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME;
            }

            if (!strcmp(VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                fullscreenInstanceExtsFound++;
                printf("found direct mode display extension\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME;
            }
#endif

            if (!strcmp(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                interopExtsFound++;
                printf("found external memory capabilities extension\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                interopExtsFound++;
                printf("found external semaphore capabilities extension\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME;
            }

            // Windows only so far. Therefore considered optional for us atm.:
            if (!strcmp(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                hdrInstanceExtsFound++;
                printf("found VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
            }

            if (!strcmp(VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
                instance_extensions[i].extensionName)) {
                if (demo->validate) {
                    demo->extension_names[demo->enabled_extension_count++] =
                        VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
                }
            }
            assert(demo->enabled_extension_count < 64);
        }

        free(instance_extensions);
    }

    if (!surfaceExtFound) {
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
    }
    if (!platformSurfaceExtFound) {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_WIN32_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_IOS_MVK)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the "
                 VK_MVK_IOS_SURFACE_EXTENSION_NAME" extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the "
                 VK_MVK_MACOS_SURFACE_EXTENSION_NAME" extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_DISPLAY_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_XCB_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_DISPLAY_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
        ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find "
                 "the " VK_KHR_XLIB_SURFACE_EXTENSION_NAME
                 " extension.\n\nDo you have a compatible "
                 "Vulkan installable client driver (ICD) installed?\nPlease "
                 "look at the Getting Started guide for additional "
                 "information.\n",
                 "vkCreateInstance Failure");
#endif
    }
    const VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = APP_SHORT_NAME,
        .applicationVersion = 0,
        .pEngineName = APP_SHORT_NAME,
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .pApplicationInfo = &app,
        .enabledLayerCount = demo->enabled_layer_count,
        .ppEnabledLayerNames = (const char *const *)instance_validation_layers,
        .enabledExtensionCount = demo->enabled_extension_count,
        .ppEnabledExtensionNames = (const char *const *)demo->extension_names,
    };

    /*
     * This is info for a temp callback to use during CreateInstance.
     * After the instance is created, we use the instance-based
     * function to register the final callback.
     */
    VkDebugReportCallbackCreateInfoEXT dbgCreateInfoTemp;
    VkValidationFlagsEXT val_flags;
    if (demo->validate) {
        dbgCreateInfoTemp.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        dbgCreateInfoTemp.pNext = inst_info.pNext;
        dbgCreateInfoTemp.pfnCallback = demo->use_break ? BreakCallback : dbgFunc;
        dbgCreateInfoTemp.pUserData = demo;
        dbgCreateInfoTemp.flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        if (demo->validate_checks_disabled) {
            val_flags.sType = VK_STRUCTURE_TYPE_VALIDATION_FLAGS_EXT;
            val_flags.pNext = NULL;
            val_flags.disabledValidationCheckCount = 1;
            VkValidationCheckEXT disabled_check = VK_VALIDATION_CHECK_ALL_EXT;
            val_flags.pDisabledValidationChecks = &disabled_check;
            dbgCreateInfoTemp.pNext = (void*)&val_flags;
        }
        inst_info.pNext = &dbgCreateInfoTemp;
    }

    uint32_t gpu_count;

    err = vkCreateInstance(&inst_info, NULL, &demo->inst);
    if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
        ERR_EXIT("Cannot find a compatible Vulkan installable client driver "
                 "(ICD).\n\nPlease look at the Getting Started guide for "
                 "additional information.\n",
                 "vkCreateInstance Failure");
    } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
        ERR_EXIT("Cannot find a specified extension library"
                 ".\nMake sure your layers path is set appropriately.\n",
                 "vkCreateInstance Failure");
    } else if (err) {
        ERR_EXIT("vkCreateInstance failed.\n\nDo you have a compatible Vulkan "
                 "installable client driver (ICD) installed?\nPlease look at "
                 "the Getting Started guide for additional information.\n",
                 "vkCreateInstance Failure");
    }

    /* Make initial call to query gpu_count, then second call for gpu info*/
    err = vkEnumeratePhysicalDevices(demo->inst, &gpu_count, NULL);
    assert(!err && gpu_count > 0);

    if (gpu_count > 0) {
        VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * gpu_count);
        err = vkEnumeratePhysicalDevices(demo->inst, &gpu_count, physical_devices);
        assert(!err);
        /* For cube demo we just grab the first physical device */
        if (demo->gpuindex < gpu_count)
            demo->gpu = physical_devices[demo->gpuindex];
        else
            demo->gpu = physical_devices[0];

        free(physical_devices);
    } else {
        ERR_EXIT("vkEnumeratePhysicalDevices reported zero accessible devices.\n\n"
                 "Do you have a compatible Vulkan installable client driver (ICD) "
                 "installed?\nPlease look at the Getting Started guide for "
                 "additional information.\n",
                 "vkEnumeratePhysicalDevices Failure");
    }

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (!get_x_lease(demo, NULL))
        printf("Could not get X-Lease for output in pass I. Retrying later...\n");
#endif

    if (displayExtFound) {
        uint32_t        display_property_count = 0;
        VkDisplayPropertiesKHR    *display_properties;
        err = vkGetPhysicalDeviceDisplayPropertiesKHR(demo->gpu, &display_property_count, NULL);
        if (err != VK_SUCCESS)
            ERR_EXIT("Cannot enumerate physical device display properties\n",
                 "vkGetPhysicalDeviceDisplayPropertiesKHR Failure");

        display_properties = calloc(display_property_count, sizeof (VkDisplayPropertiesKHR));
        err = vkGetPhysicalDeviceDisplayPropertiesKHR(demo->gpu, &display_property_count, display_properties);
        if (err != VK_SUCCESS)
            ERR_EXIT("Cannot enumerate physical device display properties\n",
                 "vkGetPhysicalDeviceDisplayPropertiesKHR Failure");

        for (uint32_t i = 0; i < display_property_count; i++) {
            printf("%p: name %s %dx%d pixels\n",
                display_properties[i].display,
                display_properties[i].displayName,
                display_properties[i].physicalResolution.width,
                display_properties[i].physicalResolution.height);
        }
    }

    /* Look for device extensions */
    uint32_t device_extension_count = 0;
    VkBool32 swapchainExtFound = 0;
    VkBool32 maintenance1ExtFound = 0;
    VkBool32 hdrmetadataExtFound = 0;
    VkBool32 externalMemoryExtFound = 0;
    VkBool32 externalSemaphoreExtFound = 0;
    VkBool32 externalMemoryFdExtFound = 0;
    VkBool32 externalSemaphoreFdExtFound = 0;
    VkBool32 fullscreenexclusiveExtFound = 0;
    VkBool32 externalMemoryWin32ExtFound = 0;
    VkBool32 externalSemaphoreWin32ExtFound = 0;
    demo->amddisplaynativehdrExtFound = 0;

/*
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
    VkBool32  = 0;
*/

    demo->enabled_extension_count = 0;
    memset(demo->extension_names, 0, sizeof(demo->extension_names));

    err = vkEnumerateDeviceExtensionProperties(demo->gpu, NULL,
                                               &device_extension_count, NULL);
    assert(!err);

    if (device_extension_count > 0) {
        VkExtensionProperties *device_extensions =
            malloc(sizeof(VkExtensionProperties) * device_extension_count);
        err = vkEnumerateDeviceExtensionProperties(
            demo->gpu, NULL, &device_extension_count, device_extensions);
        assert(!err);

        for (uint32_t i = 0; i < device_extension_count; i++) {
            if (!strcmp(VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                swapchainExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            }

#ifdef WIN32
            if (!strcmp(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                fullscreenexclusiveExtFound = 1;
                printf("found full screen exclusive extension\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME;
            }
#endif

            if (!strcmp(VK_KHR_MAINTENANCE1_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                maintenance1ExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_MAINTENANCE1_EXTENSION_NAME;
            }

            if (!strcmp(VK_EXT_HDR_METADATA_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                hdrmetadataExtFound = 1;
                printf("found VK_EXT_HDR_METADATA_EXTENSION\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
            }

            if (!strcmp(VK_AMD_DISPLAY_NATIVE_HDR_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                demo->amddisplaynativehdrExtFound = 1;
                printf("found VK_AMD_DISPLAY_NATIVE_HDR_EXTENSION\n");
                demo->extension_names[demo->enabled_extension_count++] = VK_AMD_DISPLAY_NATIVE_HDR_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalMemoryExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalSemaphoreExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalMemoryFdExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalSemaphoreFdExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
            }

#ifdef WIN32

            if (!strcmp(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalMemoryWin32ExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
            }

            if (!strcmp(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
                device_extensions[i].extensionName)) {
                externalSemaphoreWin32ExtFound = 1;
                demo->extension_names[demo->enabled_extension_count++] = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
            }
#endif
        }

        if (demo->VK_KHR_incremental_present_enabled) {
            // Even though the user "enabled" the extension via the command
            // line, we must make sure that it's enumerated for use with the
            // device.  Therefore, disable it here, and re-enable it again if
            // enumerated.
            demo->VK_KHR_incremental_present_enabled = false;
            for (uint32_t i = 0; i < device_extension_count; i++) {
                if (!strcmp(VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
                            device_extensions[i].extensionName)) {
                    demo->extension_names[demo->enabled_extension_count++] =
                        VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
                    demo->VK_KHR_incremental_present_enabled = true;
                    DbgMsg("VK_KHR_incremental_present extension enabled\n");
                }
                assert(demo->enabled_extension_count < 64);
            }
            if (!demo->VK_KHR_incremental_present_enabled) {
                DbgMsg("VK_KHR_incremental_present extension NOT AVAILABLE\n");
            }
        }

        if (demo->VK_GOOGLE_display_timing_enabled) {
            // Even though the user "enabled" the extension via the command
            // line, we must make sure that it's enumerated for use with the
            // device.  Therefore, disable it here, and re-enable it again if
            // enumerated.
            demo->VK_GOOGLE_display_timing_enabled = false;
            for (uint32_t i = 0; i < device_extension_count; i++) {
                if (!strcmp(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
                            device_extensions[i].extensionName)) {
                    demo->extension_names[demo->enabled_extension_count++] =
                        VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME;
                    demo->VK_GOOGLE_display_timing_enabled = true;
                    DbgMsg("VK_GOOGLE_display_timing extension enabled\n");
                }
                assert(demo->enabled_extension_count < 64);
            }
            if (!demo->VK_GOOGLE_display_timing_enabled) {
                DbgMsg("VK_GOOGLE_display_timing extension NOT AVAILABLE\n");
            }
        }

        free(device_extensions);
    }

    if (!swapchainExtFound || !maintenance1ExtFound) {
        ERR_EXIT("vkEnumerateDeviceExtensionProperties failed to find "
        "the swapchain or maintenance 1 extension."
        "\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
    }

#if defined(WIN32)
    if (!fullscreenexclusiveExtFound) {
#else
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (fullscreenInstanceExtsFound < 2) {
#else
    if (false) {
#endif
#endif
        ERR_EXIT("vkEnumerateDeviceExtensionProperties failed to find "
        "the fullscreen exclusive extension."
        "\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\n",
        "vkCreateInstance Failure");
    }

    if (demo->interop_enabled) {
#if defined(WIN32)
        if (!externalMemoryExtFound || !externalSemaphoreExtFound || interopExtsFound < 2 ||
            !externalMemoryWin32ExtFound || !externalSemaphoreWin32ExtFound) {
            ERR_EXIT("vkEnumerateDeviceExtensionProperties failed to find "
                "the minimum set of extensions for OpenGL->Vulkan interop"
                "\n\nDo you have a compatible "
                "Vulkan installable client driver (ICD) installed?\nPlease "
                "look at the Getting Started guide for additional "
                "information.\n",
                "vkCreateInstance Failure");
        }
#else
        if (!externalMemoryExtFound || !externalSemaphoreExtFound || interopExtsFound < 2 ||
            !externalMemoryFdExtFound || !externalSemaphoreFdExtFound) {
            ERR_EXIT("vkEnumerateDeviceExtensionProperties failed to find "
                    "the minimum set of extensions for OpenGL->Vulkan interop"
                    "\n\nDo you have a compatible "
                    "Vulkan installable client driver (ICD) installed?\nPlease "
                    "look at the Getting Started guide for additional "
                    "information.\n",
                    "vkCreateInstance Failure");
        }
#endif
    }

    if (demo->hdr_enabled) {
        if (!hdrmetadataExtFound) {
            demo->hdr_enabled = false;

            printf("\n\nWARNING WARNING WARNING WARNING WARNING WARNING WARNING\n");
            printf("vkEnumerateDeviceExtensionProperties failed to find\n"
                   "the minimum set of extensions for HDR.\n"
                   "\nDo you have a compatible Vulkan installable client\n"
                   "driver (ICD) installed?\n");
            printf("Disabling HDR output support, running in minimal\n"
                   "compatibility mode for most basic testing.\n");
            printf("WARNING WARNING WARNING WARNING WARNING WARNING WARNING\n\n");
        }
    }

    if (demo->validate) {
        demo->CreateDebugReportCallback =
            (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
                demo->inst, "vkCreateDebugReportCallbackEXT");
        demo->DestroyDebugReportCallback =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
                demo->inst, "vkDestroyDebugReportCallbackEXT");
        if (!demo->CreateDebugReportCallback) {
            ERR_EXIT(
                "GetProcAddr: Unable to find vkCreateDebugReportCallbackEXT\n",
                "vkGetProcAddr Failure");
        }
        if (!demo->DestroyDebugReportCallback) {
            ERR_EXIT(
                "GetProcAddr: Unable to find vkDestroyDebugReportCallbackEXT\n",
                "vkGetProcAddr Failure");
        }
        demo->DebugReportMessage =
            (PFN_vkDebugReportMessageEXT)vkGetInstanceProcAddr(
                demo->inst, "vkDebugReportMessageEXT");
        if (!demo->DebugReportMessage) {
            ERR_EXIT("GetProcAddr: Unable to find vkDebugReportMessageEXT\n",
                     "vkGetProcAddr Failure");
        }

        VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
        PFN_vkDebugReportCallbackEXT callback;
        callback = demo->use_break ? BreakCallback : dbgFunc;
        dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
        dbgCreateInfo.pNext = NULL;
        dbgCreateInfo.pfnCallback = callback;
        dbgCreateInfo.pUserData = demo;
        dbgCreateInfo.flags =
            VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        err = demo->CreateDebugReportCallback(demo->inst, &dbgCreateInfo, NULL,
                                              &demo->msg_callback);
        switch (err) {
        case VK_SUCCESS:
            break;
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            ERR_EXIT("CreateDebugReportCallback: out of host memory\n",
                     "CreateDebugReportCallback Failure");
            break;
        default:
            ERR_EXIT("CreateDebugReportCallback: unknown failure\n",
                     "CreateDebugReportCallback Failure");
            break;
        }
    }
    vkGetPhysicalDeviceProperties(demo->gpu, &demo->gpu_props);
    printf("Vulkan driver/gpu: %s\n", (const char*) demo->gpu_props.deviceName);

    /* Call with NULL data to get count */
    vkGetPhysicalDeviceQueueFamilyProperties(demo->gpu,
                                             &demo->queue_family_count, NULL);
    assert(demo->queue_family_count >= 1);

    demo->queue_props = (VkQueueFamilyProperties *)malloc(
        demo->queue_family_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(
        demo->gpu, &demo->queue_family_count, demo->queue_props);

    // Query fine-grained feature support for this device.
    //  If app has specific feature requirements it should check supported
    //  features based on this query
    VkPhysicalDeviceFeatures physDevFeatures;
    vkGetPhysicalDeviceFeatures(demo->gpu, &physDevFeatures);

    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceSupportKHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceFormatsKHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceFormats2KHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfacePresentModesKHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetSwapchainImagesKHR);
    GET_INSTANCE_PROC_ADDR(demo->inst, GetPhysicalDeviceSurfaceCapabilities2KHR);
}

static void demo_create_device(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;
    float queue_priorities[1] = {0.0};
    VkDeviceQueueCreateInfo queues[2];
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].pNext = NULL;
    queues[0].queueFamilyIndex = demo->graphics_queue_family_index;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = queue_priorities;
    queues[0].flags = 0;

    VkDeviceCreateInfo device = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = queues,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = demo->enabled_extension_count,
        .ppEnabledExtensionNames = (const char *const *)demo->extension_names,
        .pEnabledFeatures =
            NULL, // If specific features are required, pass them in here
    };
    if (demo->separate_present_queue) {
        queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[1].pNext = NULL;
        queues[1].queueFamilyIndex = demo->present_queue_family_index;
        queues[1].queueCount = 1;
        queues[1].pQueuePriorities = queue_priorities;
        queues[1].flags = 0;
        device.queueCreateInfoCount = 2;
    }
    err = vkCreateDevice(demo->gpu, &device, NULL, &demo->device);
    assert(!err);
}

static void demo_init_vk_swapchain(struct demo *demo) {
    VkResult U_ASSERT_ONLY err;

// Create a WSI surface for the window:
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    VkWin32SurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.hinstance = demo->connection;
    createInfo.hwnd = demo->window;

    err = vkCreateWin32SurfaceKHR(demo->inst, &createInfo, NULL, &demo->surface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    VkWaylandSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.display = demo->display;
    createInfo.surface = demo->window;

    err = vkCreateWaylandSurfaceKHR(demo->inst, &createInfo, NULL,
                                    &demo->surface);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    VkAndroidSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.window = (ANativeWindow*)(demo->window);

    err = vkCreateAndroidSurfaceKHR(demo->inst, &createInfo, NULL, &demo->surface);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    VkXlibSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.dpy = demo->display;
    createInfo.window = demo->xlib_window;

    err = vkCreateXlibSurfaceKHR(demo->inst, &createInfo, NULL,
                                     &demo->surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_DISPLAY_KHR)
    VkXcbSurfaceCreateInfoKHR createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    createInfo.pNext = NULL;
    createInfo.flags = 0;
    createInfo.connection = demo->connection;
    createInfo.window = demo->xcb_window;

    err = vkCreateXcbSurfaceKHR(demo->inst, &createInfo, NULL, &demo->surface);
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
    err = demo_create_display_surface(demo);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
    VkIOSSurfaceCreateInfoMVK surface;
    surface.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
    surface.pNext = NULL;
    surface.flags = 0;
    surface.pView = demo->window;

    err = vkCreateIOSSurfaceMVK(demo->inst, &surface, NULL, &demo->surface);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
    VkMacOSSurfaceCreateInfoMVK surface;
    surface.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
    surface.pNext = NULL;
    surface.flags = 0;
    surface.pView = demo->window;

    err = vkCreateMacOSSurfaceMVK(demo->inst, &surface, NULL, &demo->surface);
#endif
    assert(!err);

    // Iterate over each queue to learn whether it supports presenting:
    VkBool32 *supportsPresent =
        (VkBool32 *)malloc(demo->queue_family_count * sizeof(VkBool32));
    for (uint32_t i = 0; i < demo->queue_family_count; i++) {
        demo->fpGetPhysicalDeviceSurfaceSupportKHR(demo->gpu, i, demo->surface,
                                                   &supportsPresent[i]);
    }

    // Search for a graphics and a present queue in the array of queue
    // families, try to find one that supports both
    uint32_t graphicsQueueFamilyIndex = UINT32_MAX;
    uint32_t presentQueueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < demo->queue_family_count; i++) {
        if ((demo->queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            if (graphicsQueueFamilyIndex == UINT32_MAX) {
                graphicsQueueFamilyIndex = i;
            }

            if (supportsPresent[i] == VK_TRUE) {
                graphicsQueueFamilyIndex = i;
                presentQueueFamilyIndex = i;
                break;
            }
        }
    }

    if (presentQueueFamilyIndex == UINT32_MAX) {
        // If didn't find a queue that supports both graphics and present, then
        // find a separate present queue.
        for (uint32_t i = 0; i < demo->queue_family_count; ++i) {
            if (supportsPresent[i] == VK_TRUE) {
                presentQueueFamilyIndex = i;
                break;
            }
        }
    }

    // Generate error if could not find both a graphics and a present queue
    if (graphicsQueueFamilyIndex == UINT32_MAX ||
        presentQueueFamilyIndex == UINT32_MAX) {
        ERR_EXIT("Could not find both graphics and present queues\n",
                 "Swapchain Initialization Failure");
    }

    demo->graphics_queue_family_index = graphicsQueueFamilyIndex;
    demo->present_queue_family_index = presentQueueFamilyIndex;
    demo->separate_present_queue =
        (demo->graphics_queue_family_index != demo->present_queue_family_index);
    free(supportsPresent);

    demo_create_device(demo);

    // Swapchain extension:
    GET_DEVICE_PROC_ADDR(demo->device, CreateSwapchainKHR);
    GET_DEVICE_PROC_ADDR(demo->device, DestroySwapchainKHR);
    GET_DEVICE_PROC_ADDR(demo->device, GetSwapchainImagesKHR);
    GET_DEVICE_PROC_ADDR(demo->device, AcquireNextImageKHR);
    GET_DEVICE_PROC_ADDR(demo->device, QueuePresentKHR);

#if defined(WIN32)
    // External memory fd extension:
    GET_DEVICE_PROC_ADDR(demo->device, GetMemoryWin32HandleKHR);
    // Switch for fullscreen exclusive mode:
    GET_DEVICE_PROC_ADDR(demo->device, AcquireFullScreenExclusiveModeEXT);
#else
    // External memory fd extension:
    GET_DEVICE_PROC_ADDR(demo->device, GetMemoryFdKHR);
#endif

    if (demo->hdr_enabled) {
        GET_DEVICE_PROC_ADDR(demo->device, SetHdrMetadataEXT);
        if (demo->amddisplaynativehdrExtFound)
            GET_DEVICE_PROC_ADDR(demo->device, SetLocalDimmingAMD);
    }

    printf("fpSetHdrMetadataEXT = %p\n", demo->fpSetHdrMetadataEXT);
    printf("fpSetLocalDimmingAMD = %p\n", demo->fpSetLocalDimmingAMD);

    /*
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    GET_DEVICE_PROC_ADDR(demo->device, );
    */
    if (demo->VK_GOOGLE_display_timing_enabled) {
        GET_DEVICE_PROC_ADDR(demo->device, GetRefreshCycleDurationGOOGLE);
        GET_DEVICE_PROC_ADDR(demo->device, GetPastPresentationTimingGOOGLE);
    }
//    GET_DEVICE_PROC_ADDR(demo->device, RegisterDisplayEventEXT);
//    GET_DEVICE_PROC_ADDR(demo->device, GetSwapchainCounterEXT);

    vkGetDeviceQueue(demo->device, demo->graphics_queue_family_index, 0,
                     &demo->graphics_queue);

    if (!demo->separate_present_queue) {
        demo->present_queue = demo->graphics_queue;
    } else {
        vkGetDeviceQueue(demo->device, demo->present_queue_family_index, 0,
                         &demo->present_queue);
    }

    VkSurfaceCapabilitiesKHR surfCapabilities = { 0 };

#if defined(WIN32)
    VkSurfaceFullScreenExclusiveWin32InfoEXT fullscreen_exclusive_info_win32 = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT,
        .pNext = NULL,
        .hmonitor = MonitorFromWindow(demo->window, MONITOR_DEFAULTTOPRIMARY),
    };

    VkSurfaceFullScreenExclusiveInfoEXT fullscreen_exclusive_info = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT,
        .pNext = &fullscreen_exclusive_info_win32,
        .fullScreenExclusive = VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT,
    };
#endif

    const VkPhysicalDeviceSurfaceInfo2KHR surfaceinfo2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
        #if defined(WIN32)
        .pNext = &fullscreen_exclusive_info,
        #else
        .pNext = NULL,
        #endif
        .surface = demo->surface,
    };

    memset(&demo->nativeDisplayHdrMetadata, 0, sizeof(demo->nativeDisplayHdrMetadata));
    demo->nativeDisplayHdrMetadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    demo->nativeDisplayHdrMetadata.pNext = NULL;

    VkDisplayNativeHdrSurfaceCapabilitiesAMD nativeHdrCapabilitiesAMD = {
        .sType = VK_STRUCTURE_TYPE_DISPLAY_NATIVE_HDR_SURFACE_CAPABILITIES_AMD,
        .pNext = &demo->nativeDisplayHdrMetadata,
        .localDimmingSupport = VK_FALSE,
    };

    VkSurfaceCapabilities2KHR surfacecapabilities2 = {
        .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
        #ifndef VK_USE_PLATFORM_XCB_KHR
        .pNext = &nativeHdrCapabilitiesAMD,
        #else
        .pNext = NULL,
        #endif
        .surfaceCapabilities = surfCapabilities,
    };

    err = demo->fpGetPhysicalDeviceSurfaceCapabilities2KHR(demo->gpu, &surfaceinfo2,
                                                           &surfacecapabilities2);
    assert(!err);

    printf("Display native HDR properties as queried from monitor:\n");
    printf("Display Supports control of HDR local dimming: %s\n", nativeHdrCapabilitiesAMD.localDimmingSupport ? "Yes" : "No");
    printf("Display Gamut  R: [%f, %f]\n", demo->nativeDisplayHdrMetadata.displayPrimaryRed.x, demo->nativeDisplayHdrMetadata.displayPrimaryRed.y);
    printf("Display Gamut  G: [%f, %f]\n", demo->nativeDisplayHdrMetadata.displayPrimaryGreen.x, demo->nativeDisplayHdrMetadata.displayPrimaryGreen.y);
    printf("Display Gamut  B: [%f, %f]\n", demo->nativeDisplayHdrMetadata.displayPrimaryBlue.x, demo->nativeDisplayHdrMetadata.displayPrimaryBlue.y);
    printf("Display Gamut WP: [%f, %f]\n", demo->nativeDisplayHdrMetadata.whitePoint.x, demo->nativeDisplayHdrMetadata.whitePoint.y);
    printf("Display minLuminance: %f nits\n", demo->nativeDisplayHdrMetadata.minLuminance);
    printf("Display maxLuminance: %f nits\n", demo->nativeDisplayHdrMetadata.maxLuminance);
    printf("Content maxFrameAverageLightLevel: %f nits\n", demo->nativeDisplayHdrMetadata.maxFrameAverageLightLevel);
    printf("Content maxContentLightLevel: %f nits\n", demo->nativeDisplayHdrMetadata.maxContentLightLevel);

    // Get the list of VkFormat's that are supported:
    uint32_t formatCount;

    err = demo->fpGetPhysicalDeviceSurfaceFormats2KHR(demo->gpu, &surfaceinfo2,
                                                     &formatCount, NULL);
    assert(!err);
    VkSurfaceFormat2KHR *surfFormats = (VkSurfaceFormat2KHR*) malloc(formatCount * sizeof(VkSurfaceFormat2KHR));
    for (int i = 0; i < formatCount; i++) {
        surfFormats[i].sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
        surfFormats[i].pNext = NULL;
    }

    err = demo->fpGetPhysicalDeviceSurfaceFormats2KHR(demo->gpu, &surfaceinfo2,
                                                     &formatCount, surfFormats);
    assert(!err);

    printf("Number of surface [%p] formats: %d\n", surfaceinfo2.surface, formatCount);

    // If the format list includes just one entry of VK_FORMAT_UNDEFINED,
    // the surface has no preferred format.  Otherwise, at least one
    // supported format will be returned.
    if (formatCount == 1 && surfFormats[0].surfaceFormat.format == VK_FORMAT_UNDEFINED) {
        demo->format = VK_FORMAT_B8G8R8A8_UNORM;
        demo->color_space = surfFormats[0].surfaceFormat.colorSpace;
    } else {
        int i;

        assert(formatCount >= 1);

        // Try to get RGBA16F float, then RGB10A2 formats as second choice, then
        // a fallback to default RGBA8:
        demo->format = VK_FORMAT_UNDEFINED;

        for (i = 0; i < formatCount; i++) {
            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT)
                printf("[%i] For colorspace VK_COLOR_SPACE_DOLBYVISION_EXT    - ", i);

            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
                printf("[%i] For colorspace VK_COLOR_SPACE_HDR10_ST2084_EXT   - ", i);

            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT)
                printf("[%i] For colorspace VK_COLOR_SPACE_HDR10_HLG_EXT      - ", i);

            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_BT2020_LINEAR_EXT)
                printf("[%i] For colorspace VK_COLOR_SPACE_BT2020_LINEAR_EXT  - ", i);

            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_DISPLAY_NATIVE_AMD)
                printf("[%i] For colorspace VK_COLOR_SPACE_DISPLAY_NATIVE_AMD - ", i);

            if (surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                printf("[%i] For colorspace VK_COLOR_SPACE_SRGB_NONLINEAR_KHR - ", i);

            switch (surfFormats[i].surfaceFormat.format) {
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                    printf("[%i] Swapchain format VK_FORMAT_R16G16B16A16_SFLOAT\n", i);
                    break;

                case VK_FORMAT_R16G16B16A16_UNORM:
                    printf("[%i] Swapchain format VK_FORMAT_R16G16B16A16_UNORM\n", i);
                    break;

                case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
                    printf("[%i] Swapchain format VK_FORMAT_A2R10G10B10_UNORM_PACK32\n", i);
                    break;

                case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                    printf("[%i] Swapchain format VK_FORMAT_A2B10G10R10_UNORM_PACK32\n", i);
                    break;

                case VK_FORMAT_B8G8R8A8_SRGB:
                    printf("[%i] Swapchain format VK_FORMAT_B8G8R8A8_SRGB\n", i);
                    break;

                case VK_FORMAT_B8G8R8A8_UNORM:
                    printf("[%i] Swapchain format VK_FORMAT_B8G8R8A8_UNORM\n", i);
                    break;

                default:
                    printf("[%i] Swapchain format unknown %d\n", i, surfFormats[i].surfaceFormat.format);
            }
        }

        if (demo->interop_tex_format >= 2) {
            for (i = 0; (i < formatCount) && (demo->format == VK_FORMAT_UNDEFINED); i++) {
                if ((surfFormats[i].surfaceFormat.format == VK_FORMAT_R16G16B16A16_SFLOAT) &&
                    ((surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) || !demo->hdr_enabled)) {
                    printf("[%i] Using swapchain format VK_FORMAT_R16G16B16A16_SFLOAT\n", i);
                    demo->format = surfFormats[i].surfaceFormat.format;
                    demo->color_space = surfFormats[i].surfaceFormat.colorSpace;
                    demo->interop_tex_format = VK_FORMAT_R16G16B16A16_SFLOAT;
                    break;
                }
            }
        }

        /* // Not displayable on AMD Raven + Windows-10 at least!
        for (i = 0; (i < formatCount) && (demo->format == VK_FORMAT_UNDEFINED); i++) {
            if (surfFormats[i].surfaceFormat.format == VK_FORMAT_R16G16B16A16_UNORM) {
                printf("[%i] Using swapchain format VK_FORMAT_R16G16B16A16_UNORM\n", i);
                demo->format = surfFormats[i].surfaceFormat.format;
                demo->color_space = surfFormats[i].surfaceFormat.colorSpace;
                break;
            }
        }
        */

        if (demo->interop_tex_format >= 1) {
            for (i = 0; (i < formatCount) && (demo->format == VK_FORMAT_UNDEFINED); i++) {
                if ((surfFormats[i].surfaceFormat.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) &&
                    ((surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) || !demo->hdr_enabled)) {
                    printf("[%i] Using swapchain format VK_FORMAT_A2B10G10R10_UNORM_PACK32\n", i);
                    demo->format = surfFormats[i].surfaceFormat.format;
                    demo->color_space = surfFormats[i].surfaceFormat.colorSpace;
                    demo->interop_tex_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                    break;
                }
            }

            for (i = 0; (i < formatCount) && (demo->format == VK_FORMAT_UNDEFINED); i++) {
                if ((surfFormats[i].surfaceFormat.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) &&
                    ((surfFormats[i].surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) || !demo->hdr_enabled)) {
                    printf("[%i] Using swapchain format VK_FORMAT_A2R10G10B10_UNORM_PACK32\n", i);
                    demo->format = surfFormats[i].surfaceFormat.format;
                    demo->color_space = surfFormats[i].surfaceFormat.colorSpace;
                    demo->interop_tex_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                    break;
                }
            }
        }

        if (demo->format == VK_FORMAT_UNDEFINED) {
            printf("Using default fallback swapchain format VK_FORMAT_B8G8R8A8_UNORM\n");
            demo->format = surfFormats[0].surfaceFormat.format;
            demo->color_space = surfFormats[0].surfaceFormat.colorSpace;
            demo->interop_tex_format = VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    if (demo->hdr_enabled) {
        printf("Trying to enable HDR mode...\n");

        // Note: For all but VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, the application must apply the
        // OETF encoding transfer function via shader! This according to VK_EXT_swapchain_colorspace
        //
        // These work on AMD Raven Ridge: With the Monitor in FreeSync Ultimate mode.
        // Note: Only on Windows-10. On Linux + amdvlk, only VK_COLOR_SPACE_HDR10_ST2084_EXT works.

        //demo->color_space = VK_COLOR_SPACE_HDR10_ST2084_EXT;      // FreeSync standard HDR- Different characteristics depending on RGBA16G or ARGB2101010
        //demo->color_space = VK_COLOR_SPACE_BT2020_LINEAR_EXT;     // FreeSync2 HDR - Different characteristics depending on RGBA16G or ARGB2101010
        //demo->color_space = VK_COLOR_SPACE_DISPLAY_NATIVE_AMD;    // FreeSync2 HDR - Different characteristics depending on RGBA16G or ARGB2101010

        // These don't trigger HDR on AMD Raven Ridge: VK_COLOR_SPACE_HDR10_HLG_EXT VK_COLOR_SPACE_DOLBYVISION_EXT
        // Neither do non-HDR color spaces... demo->color_space = VK_COLOR_SPACE_HDR10_HLG_EXT;

        // These work on AMD Raven Ridge: With the Monitor in standard or standard FreeSync mode:
        // 8 bit format VK_FORMAT_B8G8R8A8_UNORM and
        // 10 bit formats VK_FORMAT_A2B10G10R10_UNORM_PACK32 or VK_FORMAT_A2R10G10B10_UNORM_PACK32 only work with:
        demo->color_space = VK_COLOR_SPACE_HDR10_ST2084_EXT;

        // RGBA16F 16 bpc float format always triggers HDR on AMD Raven Ridge, with all colorspaces,
        // even standard sRGB, iff FreeSync2 HDR is disabled, but not otherwise - see above.
    }

    if (!demo->interop_enabled) {
        printf("Override: OpenGL disabled - using RGBA8 texture format for static cat texture...\n");
        demo->interop_tex_format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    switch (demo->color_space) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            printf("Using colorspace VK_COLOR_SPACE_SRGB_NONLINEAR_KHR\n");
            break;

        case VK_COLOR_SPACE_BT709_LINEAR_EXT:
            printf("Using colorspace VK_COLOR_SPACE_BT709_LINEAR_EXT\n");
            break;

        case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
            printf("Using colorspace VK_COLOR_SPACE_BT709_NONLINEAR_EXT\n");
            break;

        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
            printf("Using colorspace VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT\n");
            break;

        case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
            printf("Using colorspace VK_COLOR_SPACE_BT2020_LINEAR_EXT\n");
            break;

        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
            printf("Using colorspace VK_COLOR_SPACE_HDR10_ST2084_EXT\n");
            break;

        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            printf("Using colorspace VK_COLOR_SPACE_HDR10_HLG_EXT\n");
            break;

        case VK_COLOR_SPACE_DOLBYVISION_EXT:
            printf("Using colorspace VK_COLOR_SPACE_DOLBYVISION_EXT\n");
            break;

        case VK_COLOR_SPACE_PASS_THROUGH_EXT:
            printf("Using colorspace VK_COLOR_SPACE_PASS_THROUGH_EXT\n");
            break;

        case VK_COLOR_SPACE_DISPLAY_NATIVE_AMD:
            printf("Using colorspace VK_COLOR_SPACE_DISPLAY_NATIVE_AMD\n");
            break;

        default:
            printf("Using colorspace unknown 0x%x\n", demo->color_space);
    }

    demo->quit = false;
    demo->curFrame = 0;

    // Create semaphores to synchronize acquiring presentable buffers before
    // rendering and waiting for drawing to be complete before presenting
    const VkSemaphoreCreateInfo semaphoreCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };

    // Create fences that we can use to throttle if we get too far
    // ahead of the image presents
    const VkFenceCreateInfo fence_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    for (uint32_t i = 0; i < FRAME_LAG; i++) {
        err = vkCreateFence(demo->device, &fence_ci, NULL, &demo->fences[i]);
        assert(!err);

        err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                                &demo->image_acquired_semaphores[i]);
        assert(!err);

        err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                                &demo->draw_complete_semaphores[i]);
        assert(!err);

        if (demo->separate_present_queue) {
            err = vkCreateSemaphore(demo->device, &semaphoreCreateInfo, NULL,
                                    &demo->image_ownership_semaphores[i]);
            assert(!err);
        }
    }

    // MK Create fence that we can use to wait for flip completion aka new
    // backbuffer ready aka old frontbuffer idle because flip completed.
    const VkFenceCreateInfo fence_flipcompletei = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0
    };

    err = vkCreateFence(demo->device, &fence_flipcompletei, NULL, &demo->flipcompletefence);
    assert(!err);

    demo->frame_index = 0;

    // Get Memory information and properties
    vkGetPhysicalDeviceMemoryProperties(demo->gpu, &demo->memory_properties);
}

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version UNUSED) {
    struct demo *demo = data;
    if (strcmp(interface, "wl_compositor") == 0) {
        demo->compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, 3);
        /* Todo: When xdg_shell protocol has stablized, we should move wl_shell
         * tp xdg_shell */
    } else if (strcmp(interface, "wl_shell") == 0) {
        demo->shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}

static void registry_handle_global_remove(void *data UNUSED,
                                          struct wl_registry *registry UNUSED,
                                          uint32_t name UNUSED) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global, registry_handle_global_remove};
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#endif

static void demo_init_connection(struct demo *demo) {
#if defined(VK_USE_PLATFORM_XCB_KHR)
    const xcb_setup_t *setup;
    xcb_screen_iterator_t iter;
    int scr;

    demo->connection = xcb_connect(NULL, &scr);
    if (xcb_connection_has_error(demo->connection) > 0) {
        printf("Cannot find a compatible Vulkan installable client driver "
               "(ICD).\nExiting ...\n");
        fflush(stdout);
        exit(1);
    }

    setup = xcb_get_setup(demo->connection);
    iter = xcb_setup_roots_iterator(setup);
    while (scr-- > 0)
        xcb_screen_next(&iter);

    demo->screen = iter.data;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    demo->display = wl_display_connect(NULL);

    if (demo->display == NULL) {
        printf("Cannot find a compatible Vulkan installable client driver "
               "(ICD).\nExiting ...\n");
        fflush(stdout);
        exit(1);
    }

    demo->registry = wl_display_get_registry(demo->display);
    wl_registry_add_listener(demo->registry, &registry_listener, demo);
    wl_display_dispatch(demo->display);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#endif
}

static void demo_init(struct demo *demo, int argc, char **argv) {
    vec3 eye = {0.0f, 3.0f, 2.5f};
    vec3 origin = {0, 0, 0};
    vec3 up = {0.0f, 1.0f, 0.0};

    memset(demo, 0, sizeof(*demo));
    demo->presentMode = VK_PRESENT_MODE_FIFO_KHR;
    demo->frameCount = INT32_MAX;
    demo->interop_tex_format = 1; // 10 bit unorm ~ RGB10A2 by default.
    demo->waitMsecs = 0;
    demo->output_name[0] = 0;
    demo->gpuindex = 0;
    demo->max_width = 4000;
    demo->max_height = 4000;
    demo->min_hz = 60.0;
    demo->interop_tiled_texture = false;
    demo->interop_enabled = true;
    demo->use_blit = true;
    demo->timestamping_enabled = false;
    demo->hdr_enabled = true;
    demo->local_dimming_enabled = false;
    demo->testpattern = 0;
    demo->tx = 0;
    demo->ty = 0;
    demo->rgb[0] = -1;
    demo->rgb[1] = -1;
    demo->rgb[2] = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-hdr") == 0) {
            demo->hdr_enabled = false;
            continue;
        }

        if (strcmp(argv[i], "--localdimming") == 0) {
            demo->local_dimming_enabled = true;
            continue;
        }

        if (strcmp(argv[i], "--timestamp") == 0) {
            demo->timestamping_enabled = true;
            continue;
        }

        if (strcmp(argv[i], "--useshader") == 0) {
            demo->use_blit = false;
            continue;
        }

        if (strcmp(argv[i], "--no-glinterop") == 0) {
            demo->interop_enabled = false;
            continue;
        }

        if (strcmp(argv[i], "--force-tiling") == 0) {
            demo->interop_tiled_texture = true;
            continue;
        }

        if (strcmp(argv[i], "--gpu") == 0 && i < argc - 1 &&
            sscanf(argv[i + 1], "%d", (int*) &demo->gpuindex) == 1) {
            i++;
            continue;
        }

        if (strcmp(argv[i], "--output") == 0 && i < argc - 1 &&
            sscanf(argv[i + 1], "%s", (char*) &demo->output_name) == 1) {
            i++;
            continue;
        }

        if (strcmp(argv[i], "--ifi") == 0 && i < argc - 1 &&
            sscanf(argv[i + 1], "%d", (int*) &demo->waitMsecs) == 1) {
            i++;
            demo->timestamping_enabled = true;
            continue;
        }

        if (strcmp(argv[i], "--testpattern") == 0 && i < argc - 1 &&
            sscanf(argv[i + 1], "%d", (int*) &demo->testpattern) == 1) {
            i++;
            continue;
        }

        if (strcmp(argv[i], "--translate") == 0 && i < argc - 2 &&
            sscanf(argv[i + 1], "%f", &demo->tx) == 1 &&
            sscanf(argv[i + 2], "%f", &demo->ty) == 1) {
            printf("User provided tx, ty = (%f, %f)\n", demo->tx, demo->ty);
            i += 2;
            continue;
        }

        if (strcmp(argv[i], "--rgb") == 0 && i < argc - 3 &&
            sscanf(argv[i + 1], "%f", &demo->rgb[0]) == 1 &&
            sscanf(argv[i + 2], "%f", &demo->rgb[1]) == 1 &&
            sscanf(argv[i + 3], "%f", &demo->rgb[2]) == 1) {
            printf("User provided RGB in nits = (%f, %f, %f)\n", demo->rgb[0], demo->rgb[1], demo->rgb[2]);
            i += 3;
            continue;
        }

        if (strcmp(argv[i], "--mode") == 0 && i < argc - 3 &&
            sscanf(argv[i + 1], "%i", &demo->max_width) == 1 &&
            sscanf(argv[i + 2], "%i", &demo->max_height) == 1 &&
            sscanf(argv[i + 3], "%f", &demo->min_hz) == 1) {
            printf("User provided mode limits (< %i, < %i, > %f)\n", demo->max_width, demo->max_height, demo->min_hz);
            i += 3;
            continue;
        }

        if (strcmp(argv[i], "--format") == 0 && i < argc - 1 &&
            sscanf(argv[i + 1], "%d", (int*) &demo->interop_tex_format) == 1) {
            i++;

            /*
            switch (demo->interop_tex_format) {
                case 0:
                    demo->interop_tex_format = VK_FORMAT_R8G8B8A8_UNORM;
                    break;

                case 1:
                    demo->interop_tex_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                    break;

                case 2:
                    demo->interop_tex_format = VK_FORMAT_R16G16B16A16_SFLOAT;
                    break;

                default:
                    printf("Unsupported interop texture format!\n");
                    exit(1);
            }
            */

            continue;
        }
        if (strcmp(argv[i], "--use_staging") == 0) {
            demo->use_staging_buffer = true;
            continue;
        }
        if ((strcmp(argv[i], "--present_mode") == 0) &&
                (i < argc - 1)) {
            demo->presentMode = atoi(argv[i+1]);
            i++;
            continue;
        }
        if (strcmp(argv[i], "--break") == 0) {
            demo->use_break = true;
            continue;
        }
        if (strcmp(argv[i], "--validate") == 0) {
            demo->validate = true;
            continue;
        }
        if (strcmp(argv[i], "--validate-checks-disabled") == 0) {
            demo->validate = true;
            demo->validate_checks_disabled = true;
            continue;
        }
        if (strcmp(argv[i], "--xlib") == 0) {
            fprintf(stderr, "--xlib is deprecated and no longer does anything");
            continue;
        }
        if (strcmp(argv[i], "--c") == 0 && demo->frameCount == INT32_MAX &&
            i < argc - 1 && sscanf(argv[i + 1], "%d", &demo->frameCount) == 1 &&
            demo->frameCount >= 0) {
            i++;
            continue;
        }
        if (strcmp(argv[i], "--suppress_popups") == 0) {
            demo->suppress_popups = true;
            continue;
        }
        if (strcmp(argv[i], "--display_timing") == 0) {
            demo->VK_GOOGLE_display_timing_enabled = true;
            continue;
        }
        if (strcmp(argv[i], "--incremental_present") == 0) {
            demo->VK_KHR_incremental_present_enabled = true;
            continue;
        }

#if defined(ANDROID)
        ERR_EXIT("Usage: cube [--validate]\n", "Usage");
#else
        fprintf(stderr, "Usage:\n  %s [--use_staging] [--validate] [--validate-checks-disabled] [--break] [--force-tiling] [--no-glinterop] [--useshader] [--no-hdr] [--localdimming] [--timestamp]\n"
                        "[--format <value>], with <value>: 0 = RGBA8, 1 = RGB10A2, 2 = RGBA16F [--ifi <msecs>] [--gpu <index>] [--output <RandROutputName>] [--testpattern <pattern>]\n"
                        "[--rgb <r g b>], with r,g,b in nits [--translate <x y>] [--c <framecount>] [--mode <max_width max_height min_hz>]\n"
                        "[--suppress_popups] [--incremental_present] [--display_timing] [--present_mode <present mode enum>]\n"
                        "VK_PRESENT_MODE_IMMEDIATE_KHR = %d\n"
                        "VK_PRESENT_MODE_MAILBOX_KHR = %d\n"
                        "VK_PRESENT_MODE_FIFO_KHR = %d\n"
                        "VK_PRESENT_MODE_FIFO_RELAXED_KHR = %d\n",
                APP_SHORT_NAME, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR);
        fflush(stderr);
        exit(1);
#endif
    }

    demo_init_connection(demo);

    demo->width = 512;
    demo->height = 512;

    demo_init_vk(demo);

    demo->spin_angle = 1.0f;
    demo->spin_increment = 0.01f;
    demo->pause = false;

    //mat4x4_perspective(demo->projection_matrix, (float)degreesToRadians(45.0f), 1.0f, 0.1f, 100.0f);
    mat4x4_ortho(demo->projection_matrix, -1.0, 1.0, -1.0, 1.0, -1.0, 1.0);

    //mat4x4_look_at(demo->view_matrix, eye, origin, up);
    mat4x4_identity(demo->view_matrix);
    mat4x4_identity(demo->model_matrix);

    demo->projection_matrix[1][1]*=-1;  //Flip projection matrix from GL to Vulkan orientation.
}

#if defined(VK_USE_PLATFORM_WIN32_KHR)
// Include header required for parsing the command line options.
#include <shellapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

void SetStdOutToNewConsole()
{
    // allocate a console for this app
    AllocConsole();

    // redirect unbuffered STDOUT to the console
    HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    int fileDescriptor = _open_osfhandle((intptr_t)consoleHandle, _O_TEXT);
    FILE* fp = _fdopen(fileDescriptor, "w");
    freopen_s(&fp, "CONOUT$", "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);

    // give the console window a nicer title
    SetConsoleTitle("Output");

    // give the console window a bigger buffer size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(consoleHandle, &csbi))
    {
        COORD bufferSize;
        bufferSize.X = csbi.dwSize.X;
        bufferSize.Y = 9999;
        SetConsoleScreenBufferSize(consoleHandle, bufferSize);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine,
                   int nCmdShow) {
    MSG msg;   // message
    bool done; // flag saying when app is complete
    int argc;
    char **argv;

    // Ensure wParam is initialized.
    msg.wParam = 0;

    // Use the CommandLine functions to get the command line arguments.
    // Unfortunately, Microsoft outputs
    // this information as wide characters for Unicode, and we simply want the
    // Ascii version to be compatible
    // with the non-Windows side.  So, we have to convert the information to
    // Ascii character strings.
    LPWSTR *commandLineArgs = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (NULL == commandLineArgs) {
        argc = 0;
    }

    if (argc > 0) {
        argv = (char **)malloc(sizeof(char *) * argc);
        if (argv == NULL) {
            argc = 0;
        } else {
            for (int iii = 0; iii < argc; iii++) {
                size_t wideCharLen = wcslen(commandLineArgs[iii]);
                size_t numConverted = 0;

                argv[iii] = (char *)malloc(sizeof(char) * (wideCharLen + 1));
                if (argv[iii] != NULL) {
                    wcstombs_s(&numConverted, argv[iii], wideCharLen + 1,
                               commandLineArgs[iii], wideCharLen + 1);
                }
            }
        }
    } else {
        argv = NULL;
    }

    SetStdOutToNewConsole();

    demo_init(&demo, argc, argv);

    // Free up the items we had to allocate for the command line arguments.
    if (argc > 0 && argv != NULL) {
        for (int iii = 0; iii < argc; iii++) {
            if (argv[iii] != NULL) {
                free(argv[iii]);
            }
        }
        free(argv);
    }

    demo.connection = hInstance;
    strncpy(demo.name, "cube", APP_NAME_STR_LEN);
    demo_create_window(&demo);
    demo_init_vk_swapchain(&demo);

    demo_prepare(&demo);

    // Initialize OpenGL side of OpenGL->Vulkan interop.
    demo_create_opengl_interop(&demo);

    done = false; // initialize loop condition variable

    // main message loop
    while (!done) {
        PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        if (msg.message == WM_QUIT) // check for a quit message
        {
            done = true; // if found, quit app
        } else {
            /* Translate and dispatch to event queue*/
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        RedrawWindow(demo.window, NULL, NULL, RDW_INTERNALPAINT);
    }

    demo_cleanup(&demo);

    // Give user a chance to read console output:
    ShowWindow(demo.window, SW_HIDE);
    printf("Press a character key to exit!\n");
    fflush(NULL);
    _getch();

    return (int)msg.wParam;
}

#elif defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK)
static void demo_main(struct demo *demo, void* view) {
        const char* argv[] = { "CubeSample" };
    int argc = sizeof(argv) / sizeof(char*);

    demo_init(demo, argc, (char**)argv);
    demo->window = view;
    demo_init_vk_swapchain(demo);
    demo_prepare(demo);
    demo->spin_angle = 0.4f;
}

static void demo_update_and_draw(struct demo *demo) {
    // Wait for work to finish before updating MVP.
    vkDeviceWaitIdle(demo->device);
    demo_update_data_buffer(demo);

    demo_draw(demo);
}

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/log.h>
#include <android_native_app_glue.h>
#include "android_util.h"

static bool initialized = false;
static bool active = false;
struct demo demo;

static int32_t processInput(struct android_app* app, AInputEvent* event) {
    return 0;
}

static void processCommand(struct android_app* app, int32_t cmd) {
    switch(cmd) {
        case APP_CMD_INIT_WINDOW: {
            if (app->window) {
                // We're getting a new window.  If the app is starting up, we
                // need to initialize.  If the app has already been
                // initialized, that means that we lost our previous window,
                // which means that we have a lot of work to do.  At a minimum,
                // we need to destroy the swapchain and surface associated with
                // the old window, and create a new surface and swapchain.
                // However, since there are a lot of other objects/state that
                // is tied to the swapchain, it's easiest to simply cleanup and
                // start over (i.e. use a brute-force approach of re-starting
                // the app)
                if (demo.prepared) {
                    demo_cleanup(&demo);
                }

                // Parse Intents into argc, argv
                // Use the following key to send arguments, i.e.
                // --es args "--validate"
                const char key[] = "args";
                char* appTag = (char*) APP_SHORT_NAME;
                int argc = 0;
                char** argv = get_args(app, key, appTag, &argc);

                __android_log_print(ANDROID_LOG_INFO, appTag, "argc = %i", argc);
                for (int i = 0; i < argc; i++)
                    __android_log_print(ANDROID_LOG_INFO, appTag, "argv[%i] = %s", i, argv[i]);

                demo_init(&demo, argc, argv);

                // Free the argv malloc'd by get_args
                for (int i = 0; i < argc; i++)
                    free(argv[i]);

                demo.window = (void*)app->window;
                demo_init_vk_swapchain(&demo);
                demo_prepare(&demo);
                initialized = true;
            }
            break;
        }
        case APP_CMD_GAINED_FOCUS: {
            active = true;
            break;
        }
        case APP_CMD_LOST_FOCUS: {
            active = false;
            break;
        }
    }
}

void android_main(struct android_app *app)
{
    app_dummy();

#ifdef ANDROID
    int vulkanSupport = InitVulkan();
    if (vulkanSupport == 0)
        return;
#endif

    demo.prepared = false;

    app->onAppCmd = processCommand;
    app->onInputEvent = processInput;

    while(1) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(active ? 0 : -1, NULL, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }

            if (app->destroyRequested != 0) {
                demo_cleanup(&demo);
                return;
            }
        }
        if (initialized && active) {
            demo_run(&demo);
        }
    }

}
#else
int main(int argc, char **argv) {
    struct demo demo;

    // This takes over the RandR output via DRM leasing:
    demo_init(&demo, argc, argv);

#if defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_DISPLAY_KHR)
    demo_create_xcb_window(&demo);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    demo_create_xlib_window(&demo);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    demo_create_window(&demo);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#endif

// MK:
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (demo.display) {
        demo_create_glx_opengl2(&demo);
    }
    else {
        // No X-Server, ergo no GLX based OpenGL, ergo no OpenGL interop testing.
        demo.interop_enabled = false;
    }
#endif

    demo_init_vk_swapchain(&demo);

    demo_prepare(&demo);

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
    demo_create_opengl_interop(&demo);
#endif

#if defined(VK_USE_PLATFORM_XCB_KHR) && !defined(VK_USE_PLATFORM_DISPLAY_KHR)
    demo_run_xcb(&demo);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    demo_run_xlib(&demo);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    demo_run(&demo);
#elif defined(VK_USE_PLATFORM_MIR_KHR)
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
    if (demo.display)
        demo_run_xcb(&demo);
    else
        demo_run_display(&demo);
#endif

    demo_cleanup(&demo);

    return validation_error;
}
#endif
