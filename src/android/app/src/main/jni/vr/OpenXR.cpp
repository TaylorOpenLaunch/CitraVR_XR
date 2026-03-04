/*******************************************************************************

Filename    :   OpenXR.cpp

Content     :   OpenXR initialization and shutdown code.

Authors     :   Amanda M. Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#include "OpenXR.h"

#include "utils/Common.h"
#include "utils/LogUtils.h"

#include <openxr/openxr_platform.h>

#include <array>
#include <string>
#include <unordered_set>
#include <vector>

#include <assert.h>
#include <stdarg.h>

#define BAIL_ON_ERR(fn, returnCode)                                                                \
    do {                                                                                           \
        const int32_t ret = fn;                                                                    \
        if (ret < 0) {                                                                             \
            ALOGE("ERROR ({}): {}() returned {}", __FUNCTION__, #fn, ret);                         \
            return (returnCode);                                                                   \
        }                                                                                          \
    } while (0)

namespace {
std::unordered_set<std::string> gEnabledExtensions;

const char* XrResultToSymbol(const XrResult result) {
    switch (result) {
        case XR_SUCCESS:
            return "XR_SUCCESS";
        case XR_TIMEOUT_EXPIRED:
            return "XR_TIMEOUT_EXPIRED";
        case XR_SESSION_LOSS_PENDING:
            return "XR_SESSION_LOSS_PENDING";
        case XR_EVENT_UNAVAILABLE:
            return "XR_EVENT_UNAVAILABLE";
        case XR_SPACE_BOUNDS_UNAVAILABLE:
            return "XR_SPACE_BOUNDS_UNAVAILABLE";
        case XR_SESSION_NOT_FOCUSED:
            return "XR_SESSION_NOT_FOCUSED";
        case XR_FRAME_DISCARDED:
            return "XR_FRAME_DISCARDED";
        case XR_ERROR_VALIDATION_FAILURE:
            return "XR_ERROR_VALIDATION_FAILURE";
        case XR_ERROR_RUNTIME_FAILURE:
            return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_OUT_OF_MEMORY:
            return "XR_ERROR_OUT_OF_MEMORY";
        case XR_ERROR_API_VERSION_UNSUPPORTED:
            return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case XR_ERROR_INITIALIZATION_FAILED:
            return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_FUNCTION_UNSUPPORTED:
            return "XR_ERROR_FUNCTION_UNSUPPORTED";
        case XR_ERROR_FEATURE_UNSUPPORTED:
            return "XR_ERROR_FEATURE_UNSUPPORTED";
        case XR_ERROR_EXTENSION_NOT_PRESENT:
            return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_LIMIT_REACHED:
            return "XR_ERROR_LIMIT_REACHED";
        case XR_ERROR_SIZE_INSUFFICIENT:
            return "XR_ERROR_SIZE_INSUFFICIENT";
        case XR_ERROR_HANDLE_INVALID:
            return "XR_ERROR_HANDLE_INVALID";
        case XR_ERROR_INSTANCE_LOST:
            return "XR_ERROR_INSTANCE_LOST";
        case XR_ERROR_SESSION_RUNNING:
            return "XR_ERROR_SESSION_RUNNING";
        case XR_ERROR_SESSION_NOT_RUNNING:
            return "XR_ERROR_SESSION_NOT_RUNNING";
        case XR_ERROR_SESSION_LOST:
            return "XR_ERROR_SESSION_LOST";
        case XR_ERROR_SYSTEM_INVALID:
            return "XR_ERROR_SYSTEM_INVALID";
        case XR_ERROR_PATH_INVALID:
            return "XR_ERROR_PATH_INVALID";
        case XR_ERROR_PATH_COUNT_EXCEEDED:
            return "XR_ERROR_PATH_COUNT_EXCEEDED";
        case XR_ERROR_PATH_FORMAT_INVALID:
            return "XR_ERROR_PATH_FORMAT_INVALID";
        case XR_ERROR_PATH_UNSUPPORTED:
            return "XR_ERROR_PATH_UNSUPPORTED";
        case XR_ERROR_LAYER_INVALID:
            return "XR_ERROR_LAYER_INVALID";
        case XR_ERROR_LAYER_LIMIT_EXCEEDED:
            return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
        case XR_ERROR_SWAPCHAIN_RECT_INVALID:
            return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
        case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED:
            return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
        case XR_ERROR_ACTION_TYPE_MISMATCH:
            return "XR_ERROR_ACTION_TYPE_MISMATCH";
        case XR_ERROR_SESSION_NOT_READY:
            return "XR_ERROR_SESSION_NOT_READY";
        case XR_ERROR_SESSION_NOT_STOPPING:
            return "XR_ERROR_SESSION_NOT_STOPPING";
        case XR_ERROR_TIME_INVALID:
            return "XR_ERROR_TIME_INVALID";
        case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED:
            return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
        case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
            return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
        default:
            return "XR_UNKNOWN_RESULT";
    }
}

const char* XrResultToStringSafe(const XrResult result, const XrInstance instance) {
    static thread_local char buffer[XR_MAX_RESULT_STRING_SIZE];
    memset(buffer, 0, sizeof(buffer));
    if (instance != XR_NULL_HANDLE &&
        xrResultToString(instance, result, buffer) == XR_SUCCESS &&
        buffer[0] != '\0') {
        return buffer;
    }
    return XrResultToSymbol(result);
}

void LogXrCall(const char* function, const XrResult result, const XrInstance instanceForString,
               const char* detailFmt = nullptr, ...) {
    char details[512] = {};
    if (detailFmt != nullptr) {
        va_list args;
        va_start(args, detailFmt);
        vsnprintf(details, sizeof(details), detailFmt, args);
        va_end(args);
    }

    const char* symbol = XrResultToStringSafe(result, instanceForString);
    if (details[0] != '\0') {
        XR_DIAG_LOGI("%s => %d (%s) %s", function, result, symbol, details);
    } else {
        XR_DIAG_LOGI("%s => %d (%s)", function, result, symbol);
    }
}

} // anonymous namespace

XrInstance instance = XR_NULL_HANDLE;
bool       OpenXrIsExtensionEnabled(const char* extensionName) {
    if (extensionName == nullptr) {
        return false;
    }
    return gEnabledExtensions.find(extensionName) != gEnabledExtensions.end();
}

void OXR_CheckErrors(XrResult result, const char* function, bool failOnError) {
          if (XR_FAILED(result)) {
              if (instance == XR_NULL_HANDLE) {
                  if (failOnError) {
                      FAIL("OpenXR error: %s: \"%s\" (error code 0x%x)", function, "Instance is null",
                           result);
            } else {
                      ALOGV("OpenXR error: {}: \"{}\" (error code 0x%x)", function, "Instance is null",
                            result);
            }
        } else {
                  char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
                  xrResultToString(instance, result, errorBuffer);
                  if (failOnError) {
                      FAIL("OpenXR error: %s: \"%s\" (error code 0x%x)", function, errorBuffer, result);
            } else {
                      ALOGV("OpenXR error: {}: \"{}\" (error code 0x%x)", function, errorBuffer, result);
            }
        }
    }
}

/*
   ================================================================================

   OpenXR Utility Functions

   ================================================================================
   */
namespace {
[[maybe_unused]] void XrEnumerateLayerProperties() {
    XrResult                          result;
    PFN_xrEnumerateApiLayerProperties xrEnumerateApiLayerProperties;
    OXR(result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateApiLayerProperties",
                                       (PFN_xrVoidFunction*)&xrEnumerateApiLayerProperties));
    if (result != XR_SUCCESS) {
        FAIL("Failed to get xrEnumerateApiLayerProperties function pointer.");
    }

    uint32_t numInputLayers  = 0;
    uint32_t numOutputLayers = 0;
    OXR(xrEnumerateApiLayerProperties(numInputLayers, &numOutputLayers, NULL));

    numInputLayers = numOutputLayers;

    auto layerProperties = std::vector<XrApiLayerProperties>(numOutputLayers);

    for (auto& lp : layerProperties) {
        lp.type = XR_TYPE_API_LAYER_PROPERTIES;
        lp.next = NULL;
    }

    OXR(xrEnumerateApiLayerProperties(numInputLayers, &numOutputLayers, layerProperties.data()));

    for (uint32_t i = 0; i < numOutputLayers; i++) {
        ALOGI("Found layer {}", layerProperties[i].layerName);
    }
}

std::vector<XrExtensionProperties> XrEnumerateInstanceExtensions() {
#ifndef NDEBUG
    XrEnumerateLayerProperties();
#endif

    uint32_t extensionCount = 0;
    XrResult result =
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    LogXrCall("xrEnumerateInstanceExtensionProperties(count)", result, XR_NULL_HANDLE);
    if (XR_FAILED(result)) {
        return {};
    }

    std::vector<XrExtensionProperties> extensionProperties(extensionCount);
    for (auto& ext : extensionProperties) {
        ext.type = XR_TYPE_EXTENSION_PROPERTIES;
        ext.next = nullptr;
    }

    result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                    extensionProperties.data());
    LogXrCall("xrEnumerateInstanceExtensionProperties(list)", result, XR_NULL_HANDLE,
              "count=%u", extensionCount);
    if (XR_FAILED(result)) {
        return {};
    }

    XR_DIAG_LOGI("Detected %u OpenXR instance extension(s):", extensionCount);
    for (uint32_t i = 0; i < extensionCount; ++i) {
        XR_DIAG_LOGI("  [%u] %s (specVersion=%u)", i, extensionProperties[i].extensionName,
                     extensionProperties[i].extensionVersion);
    }
    return extensionProperties;
}

void EnableExtensionIfAvailable(const char* extensionName,
                                const std::unordered_set<std::string>& availableExtensions,
                                std::vector<std::string>& enabledExtensions) {
    if (availableExtensions.find(extensionName) != availableExtensions.end()) {
        enabledExtensions.emplace_back(extensionName);
        XR_PORT_LOGI("Enabling extension: %s", extensionName);
    } else {
        XR_PORT_LOGI("Extension unavailable, skipping: %s", extensionName);
    }
}

XrInstance XrInstanceCreate(JavaVM* jvm, jobject activityObject) {
    const auto extensionProperties = XrEnumerateInstanceExtensions();
    if (extensionProperties.empty()) {
        XR_PORT_LOGE("No instance extensions were enumerated.");
        return XR_NULL_HANDLE;
    }

    std::unordered_set<std::string> availableExtensions;
    for (const auto& ext : extensionProperties) {
        availableExtensions.emplace(ext.extensionName);
    }

    static constexpr std::array<const char*, 2> kRequiredExtensions = {
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME,
    };

    for (const char* requiredExtension : kRequiredExtensions) {
        if (availableExtensions.find(requiredExtension) == availableExtensions.end()) {
            XR_PORT_LOGE("Required extension missing: %s", requiredExtension);
            return XR_NULL_HANDLE;
        }
    }

    std::vector<std::string> enabledExtensions;
    enabledExtensions.reserve(16);

    for (const char* requiredExtension : kRequiredExtensions) {
        enabledExtensions.emplace_back(requiredExtension);
        XR_PORT_LOGI("Required extension present: %s", requiredExtension);
    }

#ifdef XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_FB_PASSTHROUGH_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_FB_PASSTHROUGH_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif
#ifdef XR_META_PERFORMANCE_METRICS_EXTENSION_NAME
    EnableExtensionIfAvailable(XR_META_PERFORMANCE_METRICS_EXTENSION_NAME, availableExtensions,
                               enabledExtensions);
#endif

    std::vector<const char*> enabledExtensionNames;
    enabledExtensionNames.reserve(enabledExtensions.size());
    for (const auto& extension : enabledExtensions) {
        enabledExtensionNames.push_back(extension.c_str());
    }

    XrApplicationInfo appInfo = {};
    strcpy(appInfo.applicationName, "Citra");
    appInfo.applicationVersion = 0;
    strcpy(appInfo.engineName, "custom");
    appInfo.engineVersion = 0;
    appInfo.apiVersion    = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo ici  = {};
    ici.type                  = XR_TYPE_INSTANCE_CREATE_INFO;
    ici.next                  = nullptr;
    ici.createFlags           = 0;
    ici.applicationInfo       = appInfo;
    ici.enabledApiLayerCount  = 0;
    ici.enabledApiLayerNames  = nullptr;
    ici.enabledExtensionCount = static_cast<uint32_t>(enabledExtensionNames.size());
    ici.enabledExtensionNames = enabledExtensionNames.data();

#ifdef XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
    const bool hasAndroidCreateInstance =
        availableExtensions.find(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) !=
        availableExtensions.end();
    XrInstanceCreateInfoAndroidKHR androidCreateInfo = {
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidCreateInfo.applicationVM       = jvm;
    androidCreateInfo.applicationActivity = activityObject;
    if (hasAndroidCreateInstance) {
        ici.next = &androidCreateInfo;
        XR_PORT_LOGI("Using XR_KHR_android_create_instance chain during xrCreateInstance");
    } else {
        XR_PORT_LOGW(
            "XR_KHR_android_create_instance not available; creating instance without Android chain");
    }
#endif

    XrResult   initResult    = XR_SUCCESS;
    XrInstance instanceLocal = XR_NULL_HANDLE;
    initResult               = xrCreateInstance(&ici, &instanceLocal);
    LogXrCall("xrCreateInstance", initResult, XR_NULL_HANDLE, "enabledExtensionCount=%u",
              ici.enabledExtensionCount);
    if (XR_FAILED(initResult)) {
        XR_PORT_LOGE("Failed to create OpenXR instance.");
        return XR_NULL_HANDLE;
    }

    gEnabledExtensions.clear();
    for (const auto& extension : enabledExtensions) {
        gEnabledExtensions.insert(extension);
    }
    XR_DIAG_LOGI("Enabled %zu extension(s) for this instance.", gEnabledExtensions.size());
    for (const auto& extension : gEnabledExtensions) {
        XR_DIAG_LOGI("  enabled: %s", extension.c_str());
    }

    XrInstanceProperties instanceInfo = {};
    instanceInfo.type                 = XR_TYPE_INSTANCE_PROPERTIES;
    instanceInfo.next                 = nullptr;
    initResult                        = xrGetInstanceProperties(instanceLocal, &instanceInfo);
    LogXrCall("xrGetInstanceProperties", initResult, instanceLocal);
    if (XR_SUCCEEDED(initResult)) {
        XR_DIAG_LOGI("Runtime=%s version=%u.%u.%u", instanceInfo.runtimeName,
                     XR_VERSION_MAJOR(instanceInfo.runtimeVersion),
                     XR_VERSION_MINOR(instanceInfo.runtimeVersion),
                     XR_VERSION_PATCH(instanceInfo.runtimeVersion));
    }

    return instanceLocal;
}

// Next return code: -2
int32_t XrInitializeLoaderTrampoline(JavaVM* jvm, jobject activityObject) {
    XR_PORT_LOGI("Initializing OpenXR loader trampoline");

    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    XrResult                  result                = xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&xrInitializeLoaderKHR);
    LogXrCall("xrGetInstanceProcAddr(xrInitializeLoaderKHR)", result, XR_NULL_HANDLE);
    if (XR_FAILED(result) || xrInitializeLoaderKHR == nullptr) {
        XR_PORT_LOGE("%s: xrInitializeLoaderKHR is unavailable", __FUNCTION__);
        return -1;
    }

    XrLoaderInitInfoAndroidKHR loaderInitializeInfoAndroid = {};
    loaderInitializeInfoAndroid.type               = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loaderInitializeInfoAndroid.next               = nullptr;
    loaderInitializeInfoAndroid.applicationVM      = jvm;
    loaderInitializeInfoAndroid.applicationContext = activityObject;

    result = xrInitializeLoaderKHR(
        reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInitializeInfoAndroid));
    LogXrCall("xrInitializeLoaderKHR", result, XR_NULL_HANDLE);
    if (XR_FAILED(result)) {
        XR_PORT_LOGE("xrInitializeLoaderKHR failed");
        return -2;
    }

    XR_PORT_LOGI("OpenXR loader initialization complete");
    return 0;
}

XrSession XrSessionCreate(const XrInstance&                  localInstance,
                          const XrSystemId&                  systemId,
                          const std::unique_ptr<EglContext>& egl) {
    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding = {};
    graphicsBinding.type    = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR;
    graphicsBinding.next    = NULL;
    graphicsBinding.display = egl->mDisplay;
    graphicsBinding.config  = egl->mConfig;
    graphicsBinding.context = egl->mContext;

    XrSessionCreateInfo sessionCreateInfo = {};
    sessionCreateInfo.type                = XR_TYPE_SESSION_CREATE_INFO;
    sessionCreateInfo.next                = &graphicsBinding;
    sessionCreateInfo.createFlags         = 0;
    sessionCreateInfo.systemId            = systemId;

    XrSession session = XR_NULL_HANDLE;
    XrResult  result  = xrCreateSession(localInstance, &sessionCreateInfo, &session);
    LogXrCall("xrCreateSession", result, localInstance, "systemId=%llu",
              static_cast<unsigned long long>(systemId));
    if (XR_FAILED(result)) {
        XR_PORT_LOGE("Failed to create XR session.");
        return XR_NULL_HANDLE;
    }
    return session;
}

XrSystemId XrGetSystemId(const XrInstance& instanceLocal) {
    XrSystemId systemId = XR_NULL_SYSTEM_ID;

    XrSystemGetInfo sgi = {};
    sgi.type            = XR_TYPE_SYSTEM_GET_INFO;
    sgi.next            = NULL;
    sgi.formFactor      = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    const XrResult result = xrGetSystem(instanceLocal, &sgi, &systemId);
    LogXrCall("xrGetSystem", result, instanceLocal, "formFactor=%d systemId=%llu",
              sgi.formFactor, static_cast<unsigned long long>(systemId));
    if (XR_FAILED(result)) {
        XR_PORT_LOGE("%s: Failed to get XR system.", __FUNCTION__);
        return XR_NULL_SYSTEM_ID;
    }
    return systemId;
}

void LogSwapchainFormats(const XrInstance& instanceLocal, const XrSession& session) {
    uint32_t formatCount = 0;
    XrResult result      = xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
    LogXrCall("xrEnumerateSwapchainFormats(count)", result, instanceLocal, "count=%u", formatCount);
    if (XR_FAILED(result) || formatCount == 0) {
        return;
    }

    std::vector<int64_t> formats(formatCount);
    result = xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
    LogXrCall("xrEnumerateSwapchainFormats(list)", result, instanceLocal, "count=%u", formatCount);
    if (XR_FAILED(result)) {
        return;
    }

    for (uint32_t i = 0; i < formatCount; ++i) {
        const long long formatValue = static_cast<long long>(formats[i]);
        XR_DIAG_LOGI("swapchainFormat[%u]=0x%llx (%lld)", i,
                     static_cast<unsigned long long>(formatValue), formatValue);
    }
}

size_t GetMaxLayerCount(const XrInstance& instanceLocal, const XrSystemId& systemId) {
    XrSystemProperties systemProperties = {};
    systemProperties.type               = XR_TYPE_SYSTEM_PROPERTIES;
    const XrResult result = xrGetSystemProperties(instanceLocal, systemId, &systemProperties);
    LogXrCall("xrGetSystemProperties", result, instanceLocal, "systemId=%llu",
              static_cast<unsigned long long>(systemId));
    if (XR_FAILED(result)) {
        return 0;
    }

    ALOGV("System Properties: Name={} VendorId={}", systemProperties.systemName,
          systemProperties.vendorId);
    ALOGV("System Graphics Properties: MaxWidth={} MaxHeight={} MaxLayers={}",
          systemProperties.graphicsProperties.maxSwapchainImageWidth,
          systemProperties.graphicsProperties.maxSwapchainImageHeight,
          systemProperties.graphicsProperties.maxLayerCount);
    ALOGV("System Tracking Properties: OrientationTracking={} "
          "PositionTracking={}",
          systemProperties.trackingProperties.orientationTracking ? "True" : "False",
          systemProperties.trackingProperties.positionTracking ? "True" : "False");

    XR_DIAG_LOGI(
        "SystemProperties: name=%s vendorId=%u maxSwapchain=%ux%u maxLayers=%u tracking(orientation=%s position=%s)",
        systemProperties.systemName, systemProperties.vendorId,
        systemProperties.graphicsProperties.maxSwapchainImageWidth,
        systemProperties.graphicsProperties.maxSwapchainImageHeight,
        systemProperties.graphicsProperties.maxLayerCount,
        systemProperties.trackingProperties.orientationTracking ? "true" : "false",
        systemProperties.trackingProperties.positionTracking ? "true" : "false");

    return systemProperties.graphicsProperties.maxLayerCount;
}
} // anonymous namespace

XrInstance& OpenXr::GetInstance() { return instance; }

int32_t OpenXr::Init(JavaVM* const jvm, const jobject activityObject) {
    for (int eye = 0; eye < 2; eye++) { mViewConfigurationViews[eye] = {}; }
    BAIL_ON_ERR(OpenXRInit(jvm, activityObject), -1);
    BAIL_ON_ERR(XrViewConfigInit(), -2);
    BAIL_ON_ERR(XrSpaceInit(), -3);

    return 0;
}

// Next return code: -2
int32_t OpenXr::XrViewConfigInit() {
    // Enumerate the viewport configurations.
    uint32_t viewportConfigTypeCount = 0;
    OXR(xrEnumerateViewConfigurations(mInstance, mSystemId, 0, &viewportConfigTypeCount, NULL));

    auto viewportConfigurationTypes = std::vector<XrViewConfigurationType>(viewportConfigTypeCount);

    OXR(xrEnumerateViewConfigurations(mInstance, mSystemId, viewportConfigTypeCount,
                                      &viewportConfigTypeCount, viewportConfigurationTypes.data()));

    ALOGV("Available Viewport Configuration Types: {}", viewportConfigTypeCount);

    bool foundSupportedViewport = false;
    for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
        const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];

        ALOGV("Viewport configuration type {} : {}", viewportConfigType,
              viewportConfigType == VIEW_CONFIG_TYPE ? "Selected" : "");

        XrViewConfigurationProperties viewportConfig;
        viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
        OXR(xrGetViewConfigurationProperties(mInstance, mSystemId, viewportConfigType,
                                             &viewportConfig));
        ALOGV("FovMutable={} ConfigurationType {}",
              viewportConfig.fovMutable ? "true" : "false",
              viewportConfig.viewConfigurationType);

        uint32_t viewCount;
        OXR(xrEnumerateViewConfigurationViews(mInstance, mSystemId, viewportConfigType, 0,
                                              &viewCount, NULL));

        if (viewCount > 0) {
            auto elements = std::vector<XrViewConfigurationView>(viewCount);

            for (uint32_t e = 0; e < viewCount; e++) {
                elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
                elements[e].next = NULL;
            }

            OXR(xrEnumerateViewConfigurationViews(mInstance, mSystemId, viewportConfigType,
                                                  viewCount, &viewCount, elements.data()));

            // Log the view config info for each view type for debugging
            // purposes.
            for (uint32_t e = 0; e < viewCount; e++) {
                const XrViewConfigurationView* element = &elements[e];
                (void)element;

                ALOGV("Viewport [{}]: Recommended Width={} Height={} "
                      "SampleCount={}",
                      e, element->recommendedImageRectWidth, element->recommendedImageRectHeight,
                      element->recommendedSwapchainSampleCount);

                ALOGV("Viewport [{}]: Max Width={} Height={} SampleCount={}", e,
                      element->maxImageRectWidth, element->maxImageRectHeight,
                      element->maxSwapchainSampleCount);
            }

            // Cache the view config properties for the selected config type.
            if (viewportConfigType == VIEW_CONFIG_TYPE) {
                foundSupportedViewport = true;
                assert(viewCount == NUM_EYES);
                for (uint32_t e = 0; e < viewCount; e++) {
                    mViewConfigurationViews[e] = elements[e];
                }
            }
        } else {
            ALOGD("Empty viewport configuration type: {}", viewCount);
        }
    }
    if (!foundSupportedViewport) {
        ALOGE("No supported viewport found");
        return -1;
    }

    // Get the viewport configuration info for the chosen viewport configuration
    // type.
    mViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;

    OXR(xrGetViewConfigurationProperties(mInstance, mSystemId, VIEW_CONFIG_TYPE, &mViewportConfig));
    return 0;
}

int32_t OpenXr::XrSpaceInit() {
    bool stageSupported = false;

    uint32_t numOutputSpaces = 0;
    OXR(xrEnumerateReferenceSpaces(mSession, 0, &numOutputSpaces, NULL));

    auto referenceSpaces = std::vector<XrReferenceSpaceType>(numOutputSpaces);

    OXR(xrEnumerateReferenceSpaces(mSession, numOutputSpaces, &numOutputSpaces,
                                   referenceSpaces.data()));

    for (uint32_t i = 0; i < numOutputSpaces; i++) {
        if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
            stageSupported = true;
            break;
        }
    }

    // Create a space to the first path
    {
        XrReferenceSpaceCreateInfo spaceCreateInfo         = {};
        spaceCreateInfo.type                               = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        spaceCreateInfo.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_VIEW;
        spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
        OXR(xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mHeadSpace));

        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        OXR(xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mLocalSpace));
    }

    if (stageSupported) {
        XrReferenceSpaceCreateInfo spaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};

        spaceCreateInfo.referenceSpaceType                 = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
        OXR(xrCreateReferenceSpace(mSession, &spaceCreateInfo, &mStageSpace));
    }

    return 0;
}

void OpenXr::XrSpaceDestroy() {
    if (mHeadSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mHeadSpace));
        mHeadSpace = XR_NULL_HANDLE;
    }
    if (mLocalSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mLocalSpace));
        mLocalSpace = XR_NULL_HANDLE;
    }
    if (mStageSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mStageSpace));
        mStageSpace = XR_NULL_HANDLE;
    }
}

// Next return code: -7
int OpenXr::OpenXRInit(JavaVM* const jvm, const jobject activityObject) {

    XR_PORT_LOGI("Starting OpenXRInit");

    /////////////////////////////////////
    // Initialize OpenXR loader
    /////////////////////////////////////
    BAIL_ON_ERR(XrInitializeLoaderTrampoline(jvm, activityObject), -1);

    /////////////////////////////////////
    // Create the OpenXR instance.
    /////////////////////////////////////
    mInstance = XrInstanceCreate(jvm, activityObject);
    if (mInstance == XR_NULL_HANDLE) {
        XR_PORT_LOGE("Failed to create XR instance");
        return -2;
    }
    // Set the global used in macros
    instance = mInstance;

    mSystemId = XrGetSystemId(mInstance);
    if (mSystemId == XR_NULL_SYSTEM_ID) {
        XR_PORT_LOGE("Failed to retrieve XR system ID");
        return -3;
    }

    mMaxLayerCount = GetMaxLayerCount(mInstance, mSystemId);

    ////////////////////////////////
    // Init EGL
    ////////////////////////////////
    {
        // Get the graphics requirements.
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetOpenGLESGraphicsRequirementsKHR = NULL;
        XrResult result = xrGetInstanceProcAddr(
            mInstance, "xrGetOpenGLESGraphicsRequirementsKHR",
            (PFN_xrVoidFunction*)(&pfnGetOpenGLESGraphicsRequirementsKHR));
        LogXrCall("xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)", result, mInstance);
        if (XR_FAILED(result) || pfnGetOpenGLESGraphicsRequirementsKHR == nullptr) {
            XR_PORT_LOGE("xrGetOpenGLESGraphicsRequirementsKHR is unavailable");
            return -4;
        }

        XrGraphicsRequirementsOpenGLESKHR graphicsRequirements = {};
        graphicsRequirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
        result = pfnGetOpenGLESGraphicsRequirementsKHR(mInstance, mSystemId, &graphicsRequirements);
        LogXrCall("xrGetOpenGLESGraphicsRequirementsKHR", result, mInstance, "systemId=%llu",
                  static_cast<unsigned long long>(mSystemId));
        if (XR_FAILED(result)) {
            XR_PORT_LOGE("Failed to read OpenGLES graphics requirements");
            return -4;
        }

        {
            // Create the EGL Context
            mEglContext = std::make_unique<EglContext>();

            // Check the graphics requirements.
            int32_t eglMajor = 0;
            int32_t eglMinor = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &eglMajor);
            glGetIntegerv(GL_MINOR_VERSION, &eglMinor);
            const XrVersion eglVersion = XR_MAKE_VERSION(eglMajor, eglMinor, 0);
            if (eglVersion < graphicsRequirements.minApiVersionSupported ||
                eglVersion > graphicsRequirements.maxApiVersionSupported) {
                XR_PORT_LOGE("GLES version %d.%d not supported by runtime", eglMajor, eglMinor);
                return -5;
            }
        }
    }

    ///////////////////////////////
    // Create the OpenXR Session.
    //////////////////////////////
    mSession = XrSessionCreate(instance, mSystemId, mEglContext);
    if (mSession == XR_NULL_HANDLE) {
        XR_PORT_LOGE("Failed to create XR session");
        return -6;
    }
    LogSwapchainFormats(mInstance, mSession);
    XR_PORT_LOGI("OpenXRInit completed: systemId=%llu maxLayerCount=%zu",
                 static_cast<unsigned long long>(mSystemId), mMaxLayerCount);
    return 0;
}

void OpenXr::Shutdown() {
    XrSpaceDestroy();

    if (mSession != XR_NULL_HANDLE) {
        OXR(xrDestroySession(mSession));
        mSession = XR_NULL_HANDLE;
    }

    if (mInstance != XR_NULL_HANDLE) {
        OXR(xrDestroyInstance(mInstance));
        mInstance = XR_NULL_HANDLE;
    }

    gEnabledExtensions.clear();
}
