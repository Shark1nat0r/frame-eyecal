// The child process: an OpenXR scene (grey room, test targets) that reads the gaze every frame, which keeps the
// eye cameras at the display rate, and runs the accuracy test.
#define XR_USE_GRAPHICS_API_VULKAN
#include "scene.h"

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "gaze_test.h"
#include "geometry.h"
#include "link.h"
#include "targets.h"
#include "test_result.h"
#include "vk_util.h"

[[noreturn]] static void die(const std::string &msg) {
    fprintf(stderr, "scene: %s\n", msg.c_str());
    g_link.send("error " + msg);
    exit(1);
}
#define XR(call)                                                                                                       \
    do {                                                                                                               \
        const XrResult r_ = (call);                                                                                    \
        if (XR_FAILED(r_)) die(std::string(#call) + " failed: " + std::to_string(r_));                                 \
    } while (0)
#define VK(call)                                                                                                       \
    do {                                                                                                               \
        const VkResult r_ = (call);                                                                                    \
        if (r_ != VK_SUCCESS) die(std::string(#call) + " failed: " + std::to_string(r_));                              \
    } while (0)

static Quat quat(XrQuaternionf q) { return {q.x, q.y, q.z, q.w}; }
static V3 vec(XrVector3f v) { return {v.x, v.y, v.z}; }

namespace {

class Scene {
  public:
    explicit Scene(float hz) : hz(hz) {}
    int run();

  private:
    // setup, in this order
    void create_instance();
    void create_vulkan();
    void create_session();
    void create_swapchains();
    // the loop
    bool handle_commands(); // false once the panel is gone
    void poll_events();
    void on_state(XrSessionState st);
    void frame();
    void request_rate(int hz_now, double ts);
    bool step_test(double ts); // advances the test; true while this frame's gaze is recorded
    void read_gaze(XrTime t, bool recording);
    void end_frame(const XrFrameState &fs, bool recording);
    void draw_eye(int e, const XrView &view, bool recording);
    void draw_target(int e, const XrView &view, bool recording);
    template <class F> F xr_fn(const char *name) const {
        PFN_xrVoidFunction f = nullptr;
        XR(xrGetInstanceProcAddr(xr, name, &f));
        return reinterpret_cast<F>(f);
    }

    static constexpr double kDist = 2.0; // target distance, m
    static constexpr double kLeadS = 2.0; // before the first target
    static constexpr double kSettleS = 1.0; // per target: time to look at it ...
    static constexpr double kRecordS = 1.5; // ... and time the gaze is recorded

    float hz; // display rate to ask for
    bool has_rate = false;
    XrInstance xr = XR_NULL_HANDLE;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    VkInstance vk = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qfam = 0;
    XrSession session = XR_NULL_HANDLE;
    XrSpace view_space = XR_NULL_HANDLE, gaze_space = XR_NULL_HANDLE;
    XrActionSet aset = XR_NULL_HANDLE;
    // drawing is vkCmdClearAttachments only: no shaders
    VkRenderPass rp = VK_NULL_HANDLE;
    struct Eye {
        XrSwapchain sc = XR_NULL_HANDLE;
        int w = 0, h = 0;
        std::vector<VkFramebuffer> fb;
    } eye[2];
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;

    std::vector<Sample> samples;
    bool running = false, focused = false, quit = false, exit_req = false, testing = false;
    int cur = -1; // test visit (index into kTestOrder), -1 = lead-in
    double phase = 0, nofocus = 0; // phase start; time focus was last seen during a test
    int reported_hz = 0, rate_tries = 0;
    double next_rate_try = 0; // first request: a second after the loop starts
};

int Scene::run() {
    g_link.send("pid " + std::to_string(getpid()));
    create_instance();
    create_vulkan();
    create_session();
    create_swapchains();
    next_rate_try = now_s() + 1;
    while (!quit) {
        if (!handle_commands()) {
            if (running && !exit_req) {
                XR(xrRequestExitSession(session));
                exit_req = true;
            }
            if (!running) quit = true;
        }
        poll_events();
        if (running) frame();
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    xrDestroySession(session);
    vkDestroyDevice(dev, nullptr);
    xrDestroyInstance(xr);
    vkDestroyInstance(vk, nullptr);
    return 0;
}

void Scene::create_instance() {
    uint32_t n = 0;
    XR(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr));
    std::vector<XrExtensionProperties> ep(n, {XR_TYPE_EXTENSION_PROPERTIES});
    XR(xrEnumerateInstanceExtensionProperties(nullptr, n, &n, ep.data()));
    auto has = [&](const char *e) {
        return std::any_of(ep.begin(), ep.end(),
                           [&](const XrExtensionProperties &p) { return !strcmp(p.extensionName, e); });
    };
    if (!has(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME)) die("the OpenXR runtime has no eye gaze extension");
    std::vector<const char *> exts = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME};
    has_rate = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) && !getenv("EYECAL_NO_FB");
    if (has_rate) exts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    snprintf(ici.applicationInfo.applicationName, sizeof ici.applicationInfo.applicationName, "%s", kSceneApp);
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0); // the Frame's runtime refuses 1.1
    ici.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ici.enabledExtensionNames = exts.data();
    XR(xrCreateInstance(&ici, &xr));
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR(xrGetSystem(xr, &sgi, &sys));
    XrSystemEyeGazeInteractionPropertiesEXT egp{XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES, &egp};
    XR(xrGetSystemProperties(xr, sys, &sp));
    if (!egp.supportsEyeGazeInteraction) die(kNoEyes);
}

void Scene::create_vulkan() {
    auto get_req = xr_fn<PFN_xrGetVulkanGraphicsRequirements2KHR>("xrGetVulkanGraphicsRequirements2KHR");
    auto create_vk_instance = xr_fn<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");
    auto get_vk_device = xr_fn<PFN_xrGetVulkanGraphicsDevice2KHR>("xrGetVulkanGraphicsDevice2KHR");
    auto create_vk_device = xr_fn<PFN_xrCreateVulkanDeviceKHR>("xrCreateVulkanDeviceKHR");
    XrGraphicsRequirementsVulkan2KHR greq{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    XR(get_req(xr, sys, &greq));
    VkApplicationInfo vai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    vai.pApplicationName = "frame-eyecal";
    vai.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo vici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vici.pApplicationInfo = &vai;
    XrVulkanInstanceCreateInfoKHR xvici{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xvici.systemId = sys;
    xvici.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xvici.vulkanCreateInfo = &vici;
    VkResult vkr = VK_SUCCESS;
    XR(create_vk_instance(xr, &xvici, &vk, &vkr));
    VK(vkr);
    XrVulkanGraphicsDeviceGetInfoKHR gdi{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    gdi.systemId = sys;
    gdi.vulkanInstance = vk;
    XR(get_vk_device(xr, &gdi, &phys));
    qfam = graphics_qfam(phys);
    if (qfam == UINT32_MAX) die("no graphics queue");
    const float prio = 1;
    VkDeviceQueueCreateInfo dqci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dqci.queueFamilyIndex = qfam;
    dqci.queueCount = 1;
    dqci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &dqci;
    XrVulkanDeviceCreateInfoKHR xdci{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xdci.systemId = sys;
    xdci.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xdci.vulkanPhysicalDevice = phys;
    xdci.vulkanCreateInfo = &dci;
    XR(create_vk_device(xr, &xdci, &dev, &vkr));
    VK(vkr);
    vkGetDeviceQueue(dev, qfam, 0, &queue);
}

void Scene::create_session() { // session, view space, gaze action
    XrGraphicsBindingVulkan2KHR gb{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    gb.instance = vk;
    gb.physicalDevice = phys;
    gb.device = dev;
    gb.queueFamilyIndex = qfam;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO, &gb, 0, sys};
    XR(xrCreateSession(xr, &sci, &session));
    XrReferenceSpaceCreateInfo rsci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    rsci.poseInReferenceSpace.orientation.w = 1;
    XR(xrCreateReferenceSpace(session, &rsci, &view_space));

    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    snprintf(asci.actionSetName, sizeof asci.actionSetName, "eyecal");
    snprintf(asci.localizedActionSetName, sizeof asci.localizedActionSetName, "Frame-EyeCal");
    XR(xrCreateActionSet(xr, &asci, &aset));
    XrActionCreateInfo aci{XR_TYPE_ACTION_CREATE_INFO};
    aci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    snprintf(aci.actionName, sizeof aci.actionName, "gaze");
    snprintf(aci.localizedActionName, sizeof aci.localizedActionName, "Gaze");
    XrAction gaze = XR_NULL_HANDLE;
    XR(xrCreateAction(aset, &aci, &gaze));
    XrPath profile = XR_NULL_PATH, gaze_path = XR_NULL_PATH;
    XR(xrStringToPath(xr, "/interaction_profiles/ext/eye_gaze_interaction", &profile));
    XR(xrStringToPath(xr, "/user/eyes_ext/input/gaze_ext/pose", &gaze_path));
    XrActionSuggestedBinding sb{gaze, gaze_path};
    XrInteractionProfileSuggestedBinding ispb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    ispb.interactionProfile = profile;
    ispb.countSuggestedBindings = 1;
    ispb.suggestedBindings = &sb;
    XR(xrSuggestInteractionProfileBindings(xr, &ispb));
    XrSessionActionSetsAttachInfo sasai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    sasai.countActionSets = 1;
    sasai.actionSets = &aset;
    XR(xrAttachSessionActionSets(session, &sasai));
    XrActionSpaceCreateInfo ascri{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    ascri.action = gaze;
    ascri.poseInActionSpace.orientation.w = 1;
    XR(xrCreateActionSpace(session, &ascri, &gaze_space));
}

void Scene::create_swapchains() {
    uint32_t n = 0;
    XrViewConfigurationView vcv[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    XR(xrEnumerateViewConfigurationViews(xr, sys, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv));
    XR(xrEnumerateSwapchainFormats(session, 0, &n, nullptr));
    std::vector<int64_t> fmts(n);
    XR(xrEnumerateSwapchainFormats(session, n, &n, fmts.data()));
    auto srgb = std::find_if(fmts.begin(), fmts.end(),
                             [](int64_t f) { return f == VK_FORMAT_R8G8B8A8_SRGB || f == VK_FORMAT_B8G8R8A8_SRGB; });
    const int64_t format = srgb != fmts.end() ? *srgb : fmts[0];
    VkAttachmentDescription ad{};
    ad.format = static_cast<VkFormat>(format);
    ad.samples = VK_SAMPLE_COUNT_1_BIT;
    ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ad.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sd{};
    sd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sd.colorAttachmentCount = 1;
    sd.pColorAttachments = &ar;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &ad;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sd;
    VK(vkCreateRenderPass(dev, &rpci, nullptr, &rp));
    for (int e = 0; e < 2; e++) {
        XrSwapchainCreateInfo scci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        scci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        scci.format = format;
        scci.sampleCount = scci.faceCount = scci.arraySize = scci.mipCount = 1;
        scci.width = vcv[e].recommendedImageRectWidth;
        scci.height = vcv[e].recommendedImageRectHeight;
        XR(xrCreateSwapchain(session, &scci, &eye[e].sc));
        eye[e].w = static_cast<int>(scci.width);
        eye[e].h = static_cast<int>(scci.height);
        XR(xrEnumerateSwapchainImages(eye[e].sc, 0, &n, nullptr));
        std::vector<XrSwapchainImageVulkan2KHR> imgs(n, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        XR(xrEnumerateSwapchainImages(eye[e].sc, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(imgs.data())));
        for (const auto &img : imgs) {
            VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivci.image = img.image;
            ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format = static_cast<VkFormat>(format);
            ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view = VK_NULL_HANDLE;
            VK(vkCreateImageView(dev, &ivci, nullptr, &view));
            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass = rp;
            fbci.attachmentCount = 1;
            fbci.pAttachments = &view;
            fbci.width = scci.width;
            fbci.height = scci.height;
            fbci.layers = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            VK(vkCreateFramebuffer(dev, &fbci, nullptr, &fb));
            eye[e].fb.push_back(fb);
        }
    }
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = qfam;
    VkCommandPool pool = VK_NULL_HANDLE;
    VK(vkCreateCommandPool(dev, &cpci, nullptr, &pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool;
    cbai.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK(vkCreateFence(dev, &fci, nullptr, &fence));
}

bool Scene::handle_commands() {
    std::vector<std::string> cmds;
    bool alive = g_link.poll(cmds);
    for (const auto &c : cmds) {
        if (c == "test") {
            testing = true;
            cur = -1;
            phase = 0;
            nofocus = now_s();
            samples.clear();
        }
        if (c == "quit") alive = false;
    }
    return alive;
}

void Scene::poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(xr, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            on_state(reinterpret_cast<XrEventDataSessionStateChanged *>(&ev)->state);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

void Scene::on_state(XrSessionState st) {
    if (st == XR_SESSION_STATE_READY) {
        XrSessionBeginInfo sbi{XR_TYPE_SESSION_BEGIN_INFO};
        sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XR(xrBeginSession(session, &sbi));
        running = true;
    } else if (st == XR_SESSION_STATE_STOPPING) {
        XR(xrEndSession(session));
        running = false;
        quit = true;
    } else if (st == XR_SESSION_STATE_EXITING || st == XR_SESSION_STATE_LOSS_PENDING) {
        running = false;
        quit = true;
    }
    const bool was = focused;
    focused = st == XR_SESSION_STATE_FOCUSED;
    printf("%ld scene: session state %d\n", static_cast<long>(time(nullptr)), static_cast<int>(st));
    if (was && !focused && testing && cur >= 0) { // repeat this target when focus comes back
        while (!samples.empty() && samples.back().visit == cur) samples.pop_back();
        phase = 0;
    }
}

void Scene::frame() {
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    XR(xrWaitFrame(session, nullptr, &fs));
    XR(xrBeginFrame(session, nullptr));
    const double ts = now_s();
    const int hz_now = fs.predictedDisplayPeriod > 0
                           ? static_cast<int>(lround(1e9 / static_cast<double>(fs.predictedDisplayPeriod)))
                           : 0;
    if (hz_now != reported_hz) {
        reported_hz = hz_now;
        g_link.send("hz " + std::to_string(hz_now));
    }
    request_rate(hz_now, ts);
    const bool recording = step_test(ts);
    read_gaze(fs.predictedDisplayTime, recording);
    end_frame(fs, recording);
}

// SteamVR switches new apps to its default rate after they start, so ask (again) once frames run
void Scene::request_rate(int hz_now, double ts) {
    if (!has_rate || hz_now == static_cast<int>(lround(static_cast<double>(hz))) || rate_tries >= 3 ||
        ts <= next_rate_try)
        return;
    const XrResult rr = xr_fn<PFN_xrRequestDisplayRefreshRateFB>("xrRequestDisplayRefreshRateFB")(session, hz);
    printf("scene: requested %.0f Hz (now %d Hz): result %d\n", static_cast<double>(hz), hz_now, rr);
    rate_tries++;
    next_rate_try = ts + 3;
}

bool Scene::step_test(double ts) {
    if (testing && !focused && ts - nofocus > 10) {
        testing = false;
        g_link.send("nofocus");
    }
    if (!testing || !focused) return false;
    nofocus = ts;
    if (phase == 0) phase = ts;
    if (cur < 0 && ts - phase > kLeadS) {
        cur = 0;
        phase = ts;
    }
    if (cur >= 0 && ts - phase > kSettleS + kRecordS) {
        cur++;
        phase = ts;
    }
    if (cur >= kLooks) { // done
        testing = false;
        TestResult r;
        if (!evaluate(samples, r)) g_link.send("result none");
        else g_link.send("result " + measures_str(r) + " " + std::to_string(r.unsure));
        return false;
    }
    return cur >= 0 && ts - phase > kSettleS;
}

// The gaze, every frame and focused or not: reading it is what keeps the eye cameras at the display rate
void Scene::read_gaze(XrTime t, bool recording) {
    XrActiveActionSet aas{aset, XR_NULL_PATH};
    XrActionsSyncInfo asi{XR_TYPE_ACTIONS_SYNC_INFO};
    asi.countActiveActionSets = 1;
    asi.activeActionSets = &aas;
    xrSyncActions(session, &asi); // XR_SESSION_NOT_FOCUSED while the overlay has focus: expected
    XrEyeGazeSampleTimeEXT gst{XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT};
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION, &gst};
    xrLocateSpace(gaze_space, view_space, t, &loc);
    if (!recording) return;
    Sample s{cur, gst.time, false, 0, 0};
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    if ((loc.locationFlags & need) == need) {
        s.valid = true;
        gaze_error(quat(loc.pose.orientation), vec(loc.pose.position), kTargets[kTestOrder[cur]], kDist, s.eh, s.ev);
    }
    samples.push_back(s);
}

// Render: grey room, plus the current target (head-locked, 2 m) during a test
void Scene::end_frame(const XrFrameState &fs, bool recording) {
    XrCompositionLayerProjectionView pv[2];
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader *layers[1] = {reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer)};
    XrFrameEndInfo fei{XR_TYPE_FRAME_END_INFO};
    fei.displayTime = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (fs.shouldRender) {
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = view_space;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t n = 0;
        XR(xrLocateViews(session, &vli, &vs, 2, &n, views));
        for (int e = 0; e < 2; e++) {
            draw_eye(e, views[e], recording);
            pv[e] = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
            pv[e].pose = views[e].pose;
            pv[e].fov = views[e].fov;
            pv[e].subImage.swapchain = eye[e].sc;
            pv[e].subImage.imageRect.extent = {eye[e].w, eye[e].h};
        }
        layer.space = view_space;
        layer.viewCount = 2;
        layer.views = pv;
        fei.layerCount = 1;
        fei.layers = layers;
    }
    XR(xrEndFrame(session, &fei));
}

void Scene::draw_eye(int e, const XrView &view, bool recording) {
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo sai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XR(xrAcquireSwapchainImage(eye[e].sc, &sai, &idx));
    XrSwapchainImageWaitInfo swi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION};
    XR(xrWaitSwapchainImage(eye[e].sc, &swi));
    VkCommandBufferBeginInfo cbbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK(vkResetCommandBuffer(cmd, 0));
    VK(vkBeginCommandBuffer(cmd, &cbbi));
    VkClearValue bg{};
    bg.color = {{0.18F, 0.18F, 0.18F, 1}}; // constant grey: same pupil size every time
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = rp;
    rpbi.framebuffer = eye[e].fb[idx];
    rpbi.renderArea.extent = {static_cast<uint32_t>(eye[e].w), static_cast<uint32_t>(eye[e].h)};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &bg;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    if (testing && focused) draw_target(e, view, recording);
    vkCmdEndRenderPass(cmd);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK(vkQueueSubmit(queue, 1, &si, fence));
    VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
    VK(vkResetFences(dev, 1, &fence));
    XrSwapchainImageReleaseInfo sri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XR(xrReleaseSwapchainImage(eye[e].sc, &sri));
}

// The target as two squares: coloured (grey lead-in, yellow settling, green recording) with a black dot
void Scene::draw_target(int e, const XrView &view, bool recording) {
    const XrFovf f = view.fov;
    const double tl = tan(static_cast<double>(f.angleLeft)), tr = tan(static_cast<double>(f.angleRight));
    const double tu = tan(static_cast<double>(f.angleUp)), td = tan(static_cast<double>(f.angleDown));
    const V3 p = target_point(kTargets[cur < 0 ? 0 : kTestOrder[cur]], kDist);
    const V3 rel = qrot(conj(quat(view.pose.orientation)), p - vec(view.pose.position));
    if (rel.z >= -0.01) return; // behind the eye
    const double cx = (rel.x / -rel.z - tl) / (tr - tl) * eye[e].w, cy = (tu - rel.y / -rel.z) / (tu - td) * eye[e].h;
    const double ppd = eye[e].w / ((tr - tl) / kDeg); // pixels per degree near the centre
    VkClearColorValue colour{{0.4F, 0.4F, 0.4F, 1}}; // lead-in: grey dot in the centre
    if (recording) colour = {{0.25F, 0.75F, 0.3F, 1}};
    else if (cur >= 0) colour = {{0.9F, 0.8F, 0.1F, 1}};
    const double sizes[2] = {0.8 * ppd, 0.15 * ppd};
    const VkClearColorValue colours[2] = {colour, {{0, 0, 0, 1}}};
    for (int r = 0; r < 2; r++) {
        const int half = std::max(1, static_cast<int>(sizes[r] / 2));
        const int x0 = std::max(0, static_cast<int>(cx) - half), y0 = std::max(0, static_cast<int>(cy) - half);
        const int x1 = std::min(eye[e].w, static_cast<int>(cx) + half),
                  y1 = std::min(eye[e].h, static_cast<int>(cy) + half);
        if (x1 <= x0 || y1 <= y0) continue;
        VkClearAttachment ca{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
        ca.clearValue.color = colours[r];
        VkClearRect cr{{{x0, y0}, {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}}, 0, 1};
        vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
    }
}

} // namespace

int scene(float hz) { return Scene(hz).run(); }
