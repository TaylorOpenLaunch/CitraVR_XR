/*******************************************************************************


Filename    :   XrController.cpp
Content     :   XR-specific input handling.
Authors     :   Amanda M. Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#include "XrController.h"
#include "OpenXR.h"
#include "utils/LogUtils.h"

#include <vector>

#include <assert.h>

#if defined(DEBUG_INPUT_VERBOSE)
#define ALOG_INPUT_VERBOSE(...) ALOGD(__VAR_ARGS__)
#else
#define ALOG_INPUT_VERBOSE(...)
#endif

namespace {
bool gDisableActionPolling = false;
#if defined(CITRAVR_DISABLE_XR_CONTROLLER_CHECK) && CITRAVR_DISABLE_XR_CONTROLLER_CHECK
constexpr bool kDisableXrControllerSetup = true;
#else
constexpr bool kDisableXrControllerSetup = false;
#endif

const char* ToResultString(const XrResult result) {
    static thread_local char buffer[XR_MAX_RESULT_STRING_SIZE];
    buffer[0] = '\0';
    const XrInstance instance = OpenXr::GetInstance();
    if (instance != XR_NULL_HANDLE && xrResultToString(instance, result, buffer) == XR_SUCCESS &&
        buffer[0] != '\0') {
        return buffer;
    }
    return "XR_RESULT_UNKNOWN";
}

bool IsRecoverableInputResult(const XrResult result) {
    switch (result) {
        case XR_ERROR_PATH_UNSUPPORTED:
        case XR_ERROR_PATH_INVALID:
        case XR_ERROR_PATH_FORMAT_INVALID:
        case XR_ERROR_ACTION_TYPE_MISMATCH:
        case XR_ERROR_HANDLE_INVALID:
            return true;
        default:
            return false;
    }
}

void LogInputFallback(const char* function, const XrResult result, bool& alreadyLogged) {
    OXR_CheckErrors(result, function, false);
    if (alreadyLogged) {
        return;
    }

    const char* severity = IsRecoverableInputResult(result) ? "recoverable" : "unexpected";
    XR_PORT_LOGW("%s failed: %d (%s) [%s]; using inactive defaults",
                 function,
                 result,
                 ToResultString(result),
                 severity);
    alreadyLogged = true;
}

XrActionStateBoolean EmptyBooleanState() {
    XrActionStateBoolean state = {};
    state.type                 = XR_TYPE_ACTION_STATE_BOOLEAN;
    return state;
}

XrActionStateVector2f EmptyVector2fState() {
    XrActionStateVector2f state = {};
    state.type                  = XR_TYPE_ACTION_STATE_VECTOR2F;
    return state;
}

XrAction CreateAction(XrActionSet actionSet, XrActionType type, const char* actionName,
                      const char* localizedName, int countSubactionPaths = 0,
                      XrPath* subactionPaths = nullptr) {
    ALOG_INPUT_VERBOSE("CreateAction {}, {}" actionName, countSubactionPaths);

    XrActionCreateInfo aci = {};
    aci.type               = XR_TYPE_ACTION_CREATE_INFO;
    aci.next               = nullptr;
    aci.actionType         = type;
    if (countSubactionPaths > 0) {
        aci.countSubactionPaths = countSubactionPaths;
        aci.subactionPaths      = subactionPaths;
    }
    strcpy(aci.actionName, actionName);
    strcpy(aci.localizedActionName, localizedName ? localizedName : actionName);
    XrAction action = XR_NULL_HANDLE;
    OXR(xrCreateAction(actionSet, &aci, &action));
    return action;
}

XrActionSuggestedBinding ActionSuggestedBinding(const XrInstance& instance, XrAction action,
                                                const char* bindingString) {
    XrActionSuggestedBinding asb;
    asb.action = action;
    XrPath bindingPath;
    OXR(xrStringToPath(instance, bindingString, &bindingPath));
    asb.binding = bindingPath;
    return asb;
}

XrSpace CreateActionSpace(const XrSession& session, XrAction poseAction, XrPath subactionPath) {
    XrActionSpaceCreateInfo asci         = {};
    asci.type                            = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    asci.action                          = poseAction;
    asci.poseInActionSpace.orientation.w = 1.0f;
    asci.subactionPath                   = subactionPath;
    XrSpace actionSpace                  = XR_NULL_HANDLE;
    const XrResult createResult = xrCreateActionSpace(session, &asci, &actionSpace);
    if (XR_FAILED(createResult)) {
        static bool sLoggedActionSpaceCreateError = false;
        LogInputFallback("xrCreateActionSpace", createResult, sLoggedActionSpaceCreateError);
        return XR_NULL_HANDLE;
    }
    return actionSpace;
}

} // anonymous namespace

InputStateStatic::InputStateStatic(const XrInstance& instance, const XrSession& session) {
    if (kDisableXrControllerSetup) {
        gDisableActionPolling = true;
        XR_PORT_LOGW(
            "XR controller checks disabled by build flag; skipping action set/controller binding setup");
        return;
    }

    // Create action set.
    {
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy(actionSetInfo.actionSetName, "citra_controls");
        strcpy(actionSetInfo.localizedActionSetName, "Citra Controls");
        actionSetInfo.priority = 2;
        OXR(xrCreateActionSet(instance, &actionSetInfo, &mActionSet));
    }
    mRightHandIndexTriggerAction = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                "right_index_trigger", "Right Index Trigger");
    mLeftHandIndexTriggerAction  = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                                "left_index_trigger", "Left Index Trigger");
    mLeftMenuButtonAction = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu");
    mAButtonAction        = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "a", "A button");
    mBButtonAction        = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "b", "B button");
    mXButtonAction        = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "x", "X button");
    mYButtonAction        = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "y", "Y button");

    OXR(xrStringToPath(instance, "/user/hand/left", &mLeftHandSubactionPath));
    OXR(xrStringToPath(instance, "/user/hand/right", &mRightHandSubactionPath));
    XrPath handSubactionPaths[2] = {mLeftHandSubactionPath, mRightHandSubactionPath};

    mHandPoseAction = CreateAction(mActionSet, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", nullptr, 2,
                                   handSubactionPaths);

    mThumbStickAction = CreateAction(mActionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "thumb_stick",
                                     nullptr, 2, handSubactionPaths);

    mThumbClickAction = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_click",
                                     nullptr, 2, handSubactionPaths);

    mThumbRestTouchAction = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbresttouch",
                                         nullptr, 2, handSubactionPaths);

    mSqueezeTriggerAction = CreateAction(mActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                         "squeeze_trigger", nullptr, 2, handSubactionPaths);

    XrPath interactionProfilePath = XR_NULL_PATH;
    XrResult profilePathResult =
        xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller",
                       &interactionProfilePath);
    OXR_CheckErrors(profilePathResult, "xrStringToPath(/interaction_profiles/oculus/touch_controller)",
                    false);

    // Create bindings for Quest controllers.
    {
        // Map bindings

        std::vector<XrActionSuggestedBinding> bindings;
        bindings.push_back(
            ActionSuggestedBinding(instance, mAButtonAction, "/user/hand/right/input/a/click"));
        bindings.push_back(
            ActionSuggestedBinding(instance, mBButtonAction, "/user/hand/right/input/b/click"));
        bindings.push_back(
            ActionSuggestedBinding(instance, mXButtonAction, "/user/hand/left/input/x/click"));
        bindings.push_back(
            ActionSuggestedBinding(instance, mYButtonAction, "/user/hand/left/input/y/click"));

        bindings.push_back(ActionSuggestedBinding(instance, mLeftHandIndexTriggerAction,
                                                  "/user/hand/left/input/trigger"));
        bindings.push_back(ActionSuggestedBinding(instance, mRightHandIndexTriggerAction,
                                                  "/user/hand/right/input/trigger"));
        bindings.push_back(
            ActionSuggestedBinding(instance, mHandPoseAction, "/user/hand/left/input/aim/pose"));
        bindings.push_back(
            ActionSuggestedBinding(instance, mHandPoseAction, "/user/hand/right/input/aim/pose"));
        bindings.push_back(ActionSuggestedBinding(instance, mLeftMenuButtonAction,
                                                  "/user/hand/left/input/menu/click"));
        bindings.push_back(ActionSuggestedBinding(instance, mThumbStickAction,
                                                  "/user/hand/left/input/thumbstick"));
        bindings.push_back(ActionSuggestedBinding(instance, mThumbStickAction,
                                                  "/user/hand/right/input/thumbstick"));

        bindings.push_back(ActionSuggestedBinding(instance, mThumbClickAction,
                                                  "/user/hand/right/input/thumbstick/click"));
        bindings.push_back(ActionSuggestedBinding(instance, mThumbClickAction,
                                                  "/user/hand/left/input/thumbstick/click"));

        bindings.push_back(ActionSuggestedBinding(instance, mThumbRestTouchAction,
                                                  "/user/hand/left/input/thumbrest/touch"));
        bindings.push_back(ActionSuggestedBinding(instance, mThumbRestTouchAction,
                                                  "/user/hand/right/input/thumbrest/touch"));

        bindings.push_back(ActionSuggestedBinding(instance, mSqueezeTriggerAction,
                                                  "/user/hand/right/input/squeeze/value"));
        bindings.push_back(ActionSuggestedBinding(instance, mSqueezeTriggerAction,
                                                  "/user/hand/left/input/squeeze/value"));

        XrInteractionProfileSuggestedBinding suggestedBindings = {};
        suggestedBindings.type                   = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggestedBindings.interactionProfile     = interactionProfilePath;
        suggestedBindings.suggestedBindings      = &bindings[0];
        suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        if (XR_SUCCEEDED(profilePathResult)) {
            const XrResult suggestResult =
                xrSuggestInteractionProfileBindings(instance, &suggestedBindings);
            OXR_CheckErrors(suggestResult, "xrSuggestInteractionProfileBindings(oculus_touch)",
                            false);
            if (XR_FAILED(suggestResult)) {
                XR_PORT_LOGW(
                    "Failed to bind Oculus interaction profile. Continuing with minimal input path.");
            }
        } else {
            XR_PORT_LOGW(
                "Oculus interaction profile path unavailable. Continuing with minimal input path.");
        }

        // Attach to session
        XrSessionActionSetsAttachInfo attachInfo = {};
        attachInfo.type                          = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attachInfo.countActionSets               = 1;
        attachInfo.actionSets                    = &mActionSet;
        OXR(xrAttachSessionActionSets(session, &attachInfo));
    }
}

InputStateStatic::~InputStateStatic() {
    if (mLeftHandIndexTriggerAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mLeftHandIndexTriggerAction));
        mLeftHandIndexTriggerAction = XR_NULL_HANDLE;
    }
    if (mRightHandIndexTriggerAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mRightHandIndexTriggerAction));
        mRightHandIndexTriggerAction = XR_NULL_HANDLE;
    }
    if (mLeftMenuButtonAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mLeftMenuButtonAction));
        mLeftMenuButtonAction = XR_NULL_HANDLE;
    }
    if (mAButtonAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mAButtonAction));
        mAButtonAction = XR_NULL_HANDLE;
    }
    if (mBButtonAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mBButtonAction));
        mBButtonAction = XR_NULL_HANDLE;
    }
    if (mXButtonAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mXButtonAction));
        mXButtonAction = XR_NULL_HANDLE;
    }
    if (mYButtonAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mYButtonAction));
        mYButtonAction = XR_NULL_HANDLE;
    }
    if (mHandPoseAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mHandPoseAction));
        mHandPoseAction = XR_NULL_HANDLE;
    }
    if (mThumbStickAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mThumbStickAction));
        mThumbStickAction = XR_NULL_HANDLE;
    }
    if (mThumbClickAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mThumbClickAction));
        mThumbClickAction = XR_NULL_HANDLE;
    }
    if (mThumbRestTouchAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mThumbRestTouchAction));
        mThumbRestTouchAction = XR_NULL_HANDLE;
    }
    if (mSqueezeTriggerAction != XR_NULL_HANDLE) {
        OXR(xrDestroyAction(mSqueezeTriggerAction));
        mSqueezeTriggerAction = XR_NULL_HANDLE;
    }

    if (mLeftHandSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mLeftHandSpace));
        mLeftHandSpace = XR_NULL_HANDLE;
    }
    if (mRightHandSpace != XR_NULL_HANDLE) {
        OXR(xrDestroySpace(mRightHandSpace));
        mRightHandSpace = XR_NULL_HANDLE;
    }
    if (mActionSet != XR_NULL_HANDLE) {
        OXR(xrDestroyActionSet(mActionSet));
        mActionSet = XR_NULL_HANDLE;
    }
}

XrActionStateBoolean SyncButtonState(const XrSession& session,
                                     const XrAction&  action,
                                     const XrPath&    subactionPath = XR_NULL_PATH) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type                 = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action               = action;
    getInfo.subactionPath        = subactionPath;

    XrActionStateBoolean state = EmptyBooleanState();
    const XrResult       getResult = xrGetActionStateBoolean(session, &getInfo, &state);
    if (XR_FAILED(getResult)) {
        if (getResult == XR_ERROR_PATH_UNSUPPORTED) {
            gDisableActionPolling = true;
        }
        static bool sLoggedGetActionStateBooleanError = false;
        LogInputFallback("xrGetActionStateBoolean", getResult, sLoggedGetActionStateBooleanError);
        state = EmptyBooleanState();
    }
    return state;
}

XrActionStateVector2f SyncVector2fState(const XrSession& session, const XrAction& action,
                                        const XrPath& subactionPath = XR_NULL_PATH) {
    XrActionStateGetInfo getInfo = {};
    getInfo.type                 = XR_TYPE_ACTION_STATE_GET_INFO;
    getInfo.action               = action;
    getInfo.subactionPath        = subactionPath;

    XrActionStateVector2f state = EmptyVector2fState();
    const XrResult getResult = xrGetActionStateVector2f(session, &getInfo, &state);
    if (XR_FAILED(getResult)) {
        if (getResult == XR_ERROR_PATH_UNSUPPORTED) {
            gDisableActionPolling = true;
        }
        static bool sLoggedGetActionStateVector2fError = false;
        LogInputFallback("xrGetActionStateVector2f", getResult, sLoggedGetActionStateVector2fError);
        state = EmptyVector2fState();
    }
    return state;
}

void InputStateFrame::SyncButtonsAndThumbSticks(
    const XrSession& session, const std::unique_ptr<InputStateStatic>& staticState) {
    assert(staticState != nullptr);
    // Reset each frame to avoid stale button values when sync fails.
    mAButtonState        = EmptyBooleanState();
    mBButtonState        = EmptyBooleanState();
    mXButtonState        = EmptyBooleanState();
    mYButtonState        = EmptyBooleanState();
    mLeftMenuButtonState = EmptyBooleanState();
    mThumbStickState[LEFT_CONTROLLER]      = EmptyVector2fState();
    mThumbStickState[RIGHT_CONTROLLER]     = EmptyVector2fState();
    mThumbStickClickState[LEFT_CONTROLLER] = EmptyBooleanState();
    mThumbStickClickState[RIGHT_CONTROLLER] = EmptyBooleanState();
    mThumbrestTouchState[LEFT_CONTROLLER]   = EmptyBooleanState();
    mThumbrestTouchState[RIGHT_CONTROLLER]  = EmptyBooleanState();
    mIndexTriggerState[LEFT_CONTROLLER]     = EmptyBooleanState();
    mIndexTriggerState[RIGHT_CONTROLLER]    = EmptyBooleanState();
    mSqueezeTriggerState[LEFT_CONTROLLER]   = EmptyBooleanState();
    mSqueezeTriggerState[RIGHT_CONTROLLER]  = EmptyBooleanState();
    mIsHandActive[LEFT_CONTROLLER]          = false;
    mIsHandActive[RIGHT_CONTROLLER]         = false;

    if (gDisableActionPolling) {
        static bool sLoggedInputPollingDisabled = false;
        if (!sLoggedInputPollingDisabled) {
            XR_PORT_LOGW("Controller action polling disabled (build flag or runtime fallback)");
            sLoggedInputPollingDisabled = true;
        }
        return;
    }

    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet         = staticState->mActionSet;
    activeActionSet.subactionPath     = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo     = {};
    syncInfo.type                  = XR_TYPE_ACTIONS_SYNC_INFO;
    syncInfo.next                  = nullptr;
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets      = &activeActionSet;
    const XrResult syncResult = xrSyncActions(session, &syncInfo);
    if (XR_FAILED(syncResult)) {
        static bool sLoggedSyncActionsError = false;
        LogInputFallback("xrSyncActions", syncResult, sLoggedSyncActionsError);
        return;
    }

    // Sync button states
    mAButtonState = SyncButtonState(session, staticState->mAButtonAction);
    mBButtonState = SyncButtonState(session, staticState->mBButtonAction);
    mXButtonState = SyncButtonState(session, staticState->mXButtonAction);
    mYButtonState = SyncButtonState(session, staticState->mYButtonAction);

    mLeftMenuButtonState = SyncButtonState(session, staticState->mLeftMenuButtonAction);

    // Sync thumbstick states
    mThumbStickState[LEFT_CONTROLLER]  = SyncVector2fState(session, staticState->mThumbStickAction,
                                                           staticState->mLeftHandSubactionPath);
    mThumbStickState[RIGHT_CONTROLLER] = SyncVector2fState(session, staticState->mThumbStickAction,
                                                           staticState->mRightHandSubactionPath);

    // Sync thumbstick click states
    mThumbStickClickState[LEFT_CONTROLLER] = SyncButtonState(
        session, staticState->mThumbClickAction, staticState->mLeftHandSubactionPath);
    mThumbStickClickState[RIGHT_CONTROLLER] = SyncButtonState(
        session, staticState->mThumbClickAction, staticState->mRightHandSubactionPath);

    // Sync thumbrest touch states
    mThumbrestTouchState[LEFT_CONTROLLER] = SyncButtonState(
        session, staticState->mThumbRestTouchAction, staticState->mLeftHandSubactionPath);
    mThumbrestTouchState[RIGHT_CONTROLLER] = SyncButtonState(
        session, staticState->mThumbRestTouchAction, staticState->mRightHandSubactionPath);

    // Sync index trigger states
    // These actions are created without subaction paths; query with XR_NULL_PATH.
    mIndexTriggerState[LEFT_CONTROLLER] =
        SyncButtonState(session, staticState->mLeftHandIndexTriggerAction);
    mIndexTriggerState[RIGHT_CONTROLLER] =
        SyncButtonState(session, staticState->mRightHandIndexTriggerAction);

    // Sync squeeze trigger states
    mSqueezeTriggerState[LEFT_CONTROLLER] = SyncButtonState(
        session, staticState->mSqueezeTriggerAction, staticState->mLeftHandSubactionPath);
    mSqueezeTriggerState[RIGHT_CONTROLLER] = SyncButtonState(
        session, staticState->mSqueezeTriggerAction, staticState->mRightHandSubactionPath);

    if (!staticState->mLeftHandSpaceUnavailable && staticState->mLeftHandSpace == XR_NULL_HANDLE) {
        staticState->mLeftHandSpace = CreateActionSpace(session, staticState->mHandPoseAction,
                                                        staticState->mLeftHandSubactionPath);
        if (staticState->mLeftHandSpace == XR_NULL_HANDLE) {
            staticState->mLeftHandSpaceUnavailable = true;
            XR_PORT_LOGW("Left hand action space unavailable; hand tracking disabled for this runtime");
        }
    }
    if (!staticState->mRightHandSpaceUnavailable &&
        staticState->mRightHandSpace == XR_NULL_HANDLE) {
        staticState->mRightHandSpace = CreateActionSpace(session, staticState->mHandPoseAction,
                                                         staticState->mRightHandSubactionPath);
        if (staticState->mRightHandSpace == XR_NULL_HANDLE) {
            staticState->mRightHandSpaceUnavailable = true;
            XR_PORT_LOGW(
                "Right hand action space unavailable; hand tracking disabled for this runtime");
        }
    }

    // get the active state and pose for the two comtrollers
    if (staticState->mLeftHandSpace != XR_NULL_HANDLE) {
        XrActionStateGetInfo getInfo  = {.type          = XR_TYPE_ACTION_STATE_GET_INFO,
                                         .action        = staticState->mHandPoseAction,
                                         .subactionPath = staticState->mLeftHandSubactionPath};
        XrActionStatePose    handPose = {.type = XR_TYPE_ACTION_STATE_POSE};
        const XrResult getPoseResult = xrGetActionStatePose(session, &getInfo, &handPose);
        if (XR_SUCCEEDED(getPoseResult)) {
            mIsHandActive[LEFT_CONTROLLER] = handPose.isActive;
        } else {
            if (getPoseResult == XR_ERROR_PATH_UNSUPPORTED) {
                gDisableActionPolling = true;
            }
            static bool sLoggedGetActionStatePoseError = false;
            LogInputFallback("xrGetActionStatePose(left)", getPoseResult,
                             sLoggedGetActionStatePoseError);
            mIsHandActive[LEFT_CONTROLLER] = false;
        }
    }
    if (staticState->mRightHandSpace != XR_NULL_HANDLE) {
        XrActionStateGetInfo getInfo  = {.type          = XR_TYPE_ACTION_STATE_GET_INFO,
                                         .action        = staticState->mHandPoseAction,
                                         .subactionPath = staticState->mRightHandSubactionPath};
        XrActionStatePose    handPose = {.type = XR_TYPE_ACTION_STATE_POSE};
        const XrResult getPoseResult = xrGetActionStatePose(session, &getInfo, &handPose);
        if (XR_SUCCEEDED(getPoseResult)) {
            mIsHandActive[RIGHT_CONTROLLER] = handPose.isActive;
        } else {
            if (getPoseResult == XR_ERROR_PATH_UNSUPPORTED) {
                gDisableActionPolling = true;
            }
            static bool sLoggedGetActionStatePoseError = false;
            LogInputFallback("xrGetActionStatePose(right)", getPoseResult,
                             sLoggedGetActionStatePoseError);
            mIsHandActive[RIGHT_CONTROLLER] = false;
        }
    }
}

void InputStateFrame::SyncHandPoses(const XrSession&                         session,
                                    const std::unique_ptr<InputStateStatic>& staticState,
                                    const XrSpace&                           referenceSpace,
                                    const XrTime                             predictedDisplayTime) {
    if (staticState->mRightHandSpace != XR_NULL_HANDLE) {
        const XrResult rightLocateResult =
            xrLocateSpace(staticState->mRightHandSpace, referenceSpace, predictedDisplayTime,
                          &mHandPositions[InputStateFrame::RIGHT_CONTROLLER]);
        if (XR_SUCCEEDED(rightLocateResult)) {
            mIsHandActive[RIGHT_CONTROLLER] =
                (mHandPositions[InputStateFrame::RIGHT_CONTROLLER].locationFlags &
                 XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        } else {
            static bool sLoggedLocateSpaceError = false;
            LogInputFallback("xrLocateSpace(right)", rightLocateResult, sLoggedLocateSpaceError);
            mHandPositions[InputStateFrame::RIGHT_CONTROLLER].locationFlags = 0;
            mIsHandActive[RIGHT_CONTROLLER]                                 = false;
        }
    } else {
        mIsHandActive[RIGHT_CONTROLLER] = false;
    }

    if (staticState->mLeftHandSpace != XR_NULL_HANDLE) {
        const XrResult leftLocateResult =
            xrLocateSpace(staticState->mLeftHandSpace, referenceSpace, predictedDisplayTime,
                          &mHandPositions[InputStateFrame::LEFT_CONTROLLER]);
        if (XR_SUCCEEDED(leftLocateResult)) {
            mIsHandActive[LEFT_CONTROLLER] =
                (mHandPositions[InputStateFrame::LEFT_CONTROLLER].locationFlags &
                 XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        } else {
            static bool sLoggedLocateSpaceError = false;
            LogInputFallback("xrLocateSpace(left)", leftLocateResult, sLoggedLocateSpaceError);
            mHandPositions[InputStateFrame::LEFT_CONTROLLER].locationFlags = 0;
            mIsHandActive[LEFT_CONTROLLER]                                 = false;
        }
    } else {
        mIsHandActive[LEFT_CONTROLLER] = false;
    }

    // Determine preferred hand.
    {
        // First, determine which controllers are active
        const bool isLeftHandActive  = mIsHandActive[LEFT_CONTROLLER];
        const bool isRightHandActive = mIsHandActive[RIGHT_CONTROLLER];

        // if only one controller is active, use that one
        if (isLeftHandActive && !isRightHandActive) {
            mPreferredHand = LEFT_CONTROLLER;
        } else if (!isLeftHandActive && isRightHandActive) {
            mPreferredHand = RIGHT_CONTROLLER;
        } else if (isLeftHandActive && isRightHandActive) {
            // if both controllers are active, use whichever one last pressed
            // the index trigger
            if (mIndexTriggerState[LEFT_CONTROLLER].changedSinceLastSync &&
                mIndexTriggerState[LEFT_CONTROLLER].currentState == 1) {
                mPreferredHand = LEFT_CONTROLLER;
            }
            if (mIndexTriggerState[RIGHT_CONTROLLER].changedSinceLastSync &&
                mIndexTriggerState[RIGHT_CONTROLLER].currentState == 1) {
                mPreferredHand = RIGHT_CONTROLLER;
            } else {
                // if neither controller has pressed the index trigger, use the
                // last active controller
            }
        } else {
            // if no controllers are active, use the last active controller
        }
    }
}
