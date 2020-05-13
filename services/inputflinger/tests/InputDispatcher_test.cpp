/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../dispatcher/InputDispatcher.h"

#include <android-base/stringprintf.h>
#include <binder/Binder.h>
#include <input/Input.h>

#include <gtest/gtest.h>
#include <linux/input.h>
#include <cinttypes>
#include <unordered_set>
#include <vector>

using android::base::StringPrintf;

namespace android::inputdispatcher {

// An arbitrary time value.
static const nsecs_t ARBITRARY_TIME = 1234;

// An arbitrary device id.
static const int32_t DEVICE_ID = 1;

// An arbitrary display id.
static const int32_t DISPLAY_ID = ADISPLAY_ID_DEFAULT;

// An arbitrary injector pid / uid pair that has permission to inject events.
static const int32_t INJECTOR_PID = 999;
static const int32_t INJECTOR_UID = 1001;

struct PointF {
    float x;
    float y;
};

/**
 * Return a DOWN key event with KEYCODE_A.
 */
static KeyEvent getTestKeyEvent() {
    KeyEvent event;

    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD, ADISPLAY_ID_NONE,
                     INVALID_HMAC, AKEY_EVENT_ACTION_DOWN, 0, AKEYCODE_A, KEY_A, AMETA_NONE, 0,
                     ARBITRARY_TIME, ARBITRARY_TIME);
    return event;
}

// --- FakeInputDispatcherPolicy ---

class FakeInputDispatcherPolicy : public InputDispatcherPolicyInterface {
    InputDispatcherConfiguration mConfig;

protected:
    virtual ~FakeInputDispatcherPolicy() {
    }

public:
    FakeInputDispatcherPolicy() {
    }

    void assertFilterInputEventWasCalled(const NotifyKeyArgs& args) {
        assertFilterInputEventWasCalled(AINPUT_EVENT_TYPE_KEY, args.eventTime, args.action,
                                        args.displayId);
    }

    void assertFilterInputEventWasCalled(const NotifyMotionArgs& args) {
        assertFilterInputEventWasCalled(AINPUT_EVENT_TYPE_MOTION, args.eventTime, args.action,
                                        args.displayId);
    }

    void assertFilterInputEventWasNotCalled() { ASSERT_EQ(nullptr, mFilteredEvent); }

    void assertNotifyConfigurationChangedWasCalled(nsecs_t when) {
        ASSERT_TRUE(mConfigurationChangedTime)
                << "Timed out waiting for configuration changed call";
        ASSERT_EQ(*mConfigurationChangedTime, when);
        mConfigurationChangedTime = std::nullopt;
    }

    void assertNotifySwitchWasCalled(const NotifySwitchArgs& args) {
        ASSERT_TRUE(mLastNotifySwitch);
        // We do not check id because it is not exposed to the policy
        EXPECT_EQ(args.eventTime, mLastNotifySwitch->eventTime);
        EXPECT_EQ(args.policyFlags, mLastNotifySwitch->policyFlags);
        EXPECT_EQ(args.switchValues, mLastNotifySwitch->switchValues);
        EXPECT_EQ(args.switchMask, mLastNotifySwitch->switchMask);
        mLastNotifySwitch = std::nullopt;
    }

    void assertOnPointerDownEquals(const sp<IBinder>& touchedToken) {
        ASSERT_EQ(touchedToken, mOnPointerDownToken);
        mOnPointerDownToken.clear();
    }

    void assertOnPointerDownWasNotCalled() {
        ASSERT_TRUE(mOnPointerDownToken == nullptr)
                << "Expected onPointerDownOutsideFocus to not have been called";
    }

    void setKeyRepeatConfiguration(nsecs_t timeout, nsecs_t delay) {
        mConfig.keyRepeatTimeout = timeout;
        mConfig.keyRepeatDelay = delay;
    }

private:
    std::unique_ptr<InputEvent> mFilteredEvent;
    std::optional<nsecs_t> mConfigurationChangedTime;
    sp<IBinder> mOnPointerDownToken;
    std::optional<NotifySwitchArgs> mLastNotifySwitch;

    virtual void notifyConfigurationChanged(nsecs_t when) override {
        mConfigurationChangedTime = when;
    }

    virtual nsecs_t notifyANR(const sp<InputApplicationHandle>&,
            const sp<IBinder>&,
            const std::string&) {
        return 0;
    }

    virtual void notifyInputChannelBroken(const sp<IBinder>&) {
    }

    virtual void notifyFocusChanged(const sp<IBinder>&, const sp<IBinder>&) {
    }

    virtual void getDispatcherConfiguration(InputDispatcherConfiguration* outConfig) {
        *outConfig = mConfig;
    }

    virtual bool filterInputEvent(const InputEvent* inputEvent, uint32_t policyFlags) override {
        switch (inputEvent->getType()) {
            case AINPUT_EVENT_TYPE_KEY: {
                const KeyEvent* keyEvent = static_cast<const KeyEvent*>(inputEvent);
                mFilteredEvent = std::make_unique<KeyEvent>(*keyEvent);
                break;
            }

            case AINPUT_EVENT_TYPE_MOTION: {
                const MotionEvent* motionEvent = static_cast<const MotionEvent*>(inputEvent);
                mFilteredEvent = std::make_unique<MotionEvent>(*motionEvent);
                break;
            }
        }
        return true;
    }

    virtual void interceptKeyBeforeQueueing(const KeyEvent*, uint32_t&) {
    }

    virtual void interceptMotionBeforeQueueing(int32_t, nsecs_t, uint32_t&) {
    }

    virtual nsecs_t interceptKeyBeforeDispatching(const sp<IBinder>&,
            const KeyEvent*, uint32_t) {
        return 0;
    }

    virtual bool dispatchUnhandledKey(const sp<IBinder>&,
            const KeyEvent*, uint32_t, KeyEvent*) {
        return false;
    }

    virtual void notifySwitch(nsecs_t when, uint32_t switchValues, uint32_t switchMask,
                              uint32_t policyFlags) override {
        /** We simply reconstruct NotifySwitchArgs in policy because InputDispatcher is
         * essentially a passthrough for notifySwitch.
         */
        mLastNotifySwitch = NotifySwitchArgs(1 /*id*/, when, policyFlags, switchValues, switchMask);
    }

    virtual void pokeUserActivity(nsecs_t, int32_t) {
    }

    virtual bool checkInjectEventsPermissionNonReentrant(int32_t, int32_t) {
        return false;
    }

    virtual void onPointerDownOutsideFocus(const sp<IBinder>& newToken) {
        mOnPointerDownToken = newToken;
    }

    void assertFilterInputEventWasCalled(int type, nsecs_t eventTime, int32_t action,
                                         int32_t displayId) {
        ASSERT_NE(nullptr, mFilteredEvent) << "Expected filterInputEvent() to have been called.";
        ASSERT_EQ(mFilteredEvent->getType(), type);

        if (type == AINPUT_EVENT_TYPE_KEY) {
            const KeyEvent& keyEvent = static_cast<const KeyEvent&>(*mFilteredEvent);
            EXPECT_EQ(keyEvent.getEventTime(), eventTime);
            EXPECT_EQ(keyEvent.getAction(), action);
            EXPECT_EQ(keyEvent.getDisplayId(), displayId);
        } else if (type == AINPUT_EVENT_TYPE_MOTION) {
            const MotionEvent& motionEvent = static_cast<const MotionEvent&>(*mFilteredEvent);
            EXPECT_EQ(motionEvent.getEventTime(), eventTime);
            EXPECT_EQ(motionEvent.getAction(), action);
            EXPECT_EQ(motionEvent.getDisplayId(), displayId);
        } else {
            FAIL() << "Unknown type: " << type;
        }

        mFilteredEvent = nullptr;
    }
};

// --- HmacKeyManagerTest ---

class HmacKeyManagerTest : public testing::Test {
protected:
    HmacKeyManager mHmacKeyManager;
};

/**
 * Ensure that separate calls to sign the same data are generating the same key.
 * We avoid asserting against INVALID_HMAC. Since the key is random, there is a non-zero chance
 * that a specific key and data combination would produce INVALID_HMAC, which would cause flaky
 * tests.
 */
TEST_F(HmacKeyManagerTest, GeneratedHmac_IsConsistent) {
    KeyEvent event = getTestKeyEvent();
    VerifiedKeyEvent verifiedEvent = verifiedKeyEventFromKeyEvent(event);

    std::array<uint8_t, 32> hmac1 = mHmacKeyManager.sign(verifiedEvent);
    std::array<uint8_t, 32> hmac2 = mHmacKeyManager.sign(verifiedEvent);
    ASSERT_EQ(hmac1, hmac2);
}

/**
 * Ensure that changes in VerifiedKeyEvent produce a different hmac.
 */
TEST_F(HmacKeyManagerTest, GeneratedHmac_ChangesWhenFieldsChange) {
    KeyEvent event = getTestKeyEvent();
    VerifiedKeyEvent verifiedEvent = verifiedKeyEventFromKeyEvent(event);
    std::array<uint8_t, 32> initialHmac = mHmacKeyManager.sign(verifiedEvent);

    verifiedEvent.deviceId += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.source += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.eventTimeNanos += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.displayId += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.action += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.downTimeNanos += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.flags += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.keyCode += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.scanCode += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.metaState += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));

    verifiedEvent.repeatCount += 1;
    ASSERT_NE(initialHmac, mHmacKeyManager.sign(verifiedEvent));
}

// --- InputDispatcherTest ---

class InputDispatcherTest : public testing::Test {
protected:
    sp<FakeInputDispatcherPolicy> mFakePolicy;
    sp<InputDispatcher> mDispatcher;

    virtual void SetUp() override {
        mFakePolicy = new FakeInputDispatcherPolicy();
        mDispatcher = new InputDispatcher(mFakePolicy);
        mDispatcher->setInputDispatchMode(/*enabled*/ true, /*frozen*/ false);
        //Start InputDispatcher thread
        ASSERT_EQ(OK, mDispatcher->start());
    }

    virtual void TearDown() override {
        ASSERT_EQ(OK, mDispatcher->stop());
        mFakePolicy.clear();
        mDispatcher.clear();
    }
};


TEST_F(InputDispatcherTest, InjectInputEvent_ValidatesKeyEvents) {
    KeyEvent event;

    // Rejects undefined key actions.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD, ADISPLAY_ID_NONE,
                     INVALID_HMAC,
                     /*action*/ -1, 0, AKEYCODE_A, KEY_A, AMETA_NONE, 0, ARBITRARY_TIME,
                     ARBITRARY_TIME);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject key events with undefined action.";

    // Rejects ACTION_MULTIPLE since it is not supported despite being defined in the API.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD, ADISPLAY_ID_NONE,
                     INVALID_HMAC, AKEY_EVENT_ACTION_MULTIPLE, 0, AKEYCODE_A, KEY_A, AMETA_NONE, 0,
                     ARBITRARY_TIME, ARBITRARY_TIME);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject key events with ACTION_MULTIPLE.";
}

TEST_F(InputDispatcherTest, InjectInputEvent_ValidatesMotionEvents) {
    MotionEvent event;
    PointerProperties pointerProperties[MAX_POINTERS + 1];
    PointerCoords pointerCoords[MAX_POINTERS + 1];
    for (int i = 0; i <= MAX_POINTERS; i++) {
        pointerProperties[i].clear();
        pointerProperties[i].id = i;
        pointerCoords[i].clear();
    }

    // Some constants commonly used below
    constexpr int32_t source = AINPUT_SOURCE_TOUCHSCREEN;
    constexpr int32_t edgeFlags = AMOTION_EVENT_EDGE_FLAG_NONE;
    constexpr int32_t metaState = AMETA_NONE;
    constexpr MotionClassification classification = MotionClassification::NONE;

    // Rejects undefined motion actions.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     /*action*/ -1, 0, 0, edgeFlags, metaState, 0, classification, 1 /* xScale */,
                     1 /* yScale */, 0, 0, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with undefined action.";

    // Rejects pointer down with invalid index.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_DOWN |
                             (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, 1 /* xScale */, 1 /* yScale */,
                     0, 0, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer down index too large.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_DOWN |
                             (~0U << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, 1 /* xScale */, 1 /* yScale */,
                     0, 0, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer down index too small.";

    // Rejects pointer up with invalid index.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_UP |
                             (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, 1 /* xScale */, 1 /* yScale */,
                     0, 0, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer up index too large.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_POINTER_UP |
                             (~0U << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
                     0, 0, edgeFlags, metaState, 0, classification, 1 /* xScale */, 1 /* yScale */,
                     0, 0, 0, 0, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer up index too small.";

    // Rejects motion events with invalid number of pointers.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     1 /* xScale */, 1 /* yScale */, 0, 0, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 0, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with 0 pointers.";

    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     1 /* xScale */, 1 /* yScale */, 0, 0, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ MAX_POINTERS + 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with more than MAX_POINTERS pointers.";

    // Rejects motion events with invalid pointer ids.
    pointerProperties[0].id = -1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     1 /* xScale */, 1 /* yScale */, 0, 0, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer ids less than 0.";

    pointerProperties[0].id = MAX_POINTER_ID + 1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     1 /* xScale */, 1 /* yScale */, 0, 0, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with pointer ids greater than MAX_POINTER_ID.";

    // Rejects motion events with duplicate pointer ids.
    pointerProperties[0].id = 1;
    pointerProperties[1].id = 1;
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, DISPLAY_ID, INVALID_HMAC,
                     AMOTION_EVENT_ACTION_DOWN, 0, 0, edgeFlags, metaState, 0, classification,
                     1 /* xScale */, 1 /* yScale */, 0, 0, 0, 0,
                     AMOTION_EVENT_INVALID_CURSOR_POSITION, AMOTION_EVENT_INVALID_CURSOR_POSITION,
                     ARBITRARY_TIME, ARBITRARY_TIME,
                     /*pointerCount*/ 2, pointerProperties, pointerCoords);
    ASSERT_EQ(INPUT_EVENT_INJECTION_FAILED, mDispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_NONE, 0, 0))
            << "Should reject motion events with duplicate pointer ids.";
}

/* Test InputDispatcher for notifyConfigurationChanged and notifySwitch events */

TEST_F(InputDispatcherTest, NotifyConfigurationChanged_CallsPolicy) {
    constexpr nsecs_t eventTime = 20;
    NotifyConfigurationChangedArgs args(10 /*id*/, eventTime);
    mDispatcher->notifyConfigurationChanged(&args);
    ASSERT_TRUE(mDispatcher->waitForIdle());

    mFakePolicy->assertNotifyConfigurationChangedWasCalled(eventTime);
}

TEST_F(InputDispatcherTest, NotifySwitch_CallsPolicy) {
    NotifySwitchArgs args(10 /*id*/, 20 /*eventTime*/, 0 /*policyFlags*/, 1 /*switchValues*/,
                          2 /*switchMask*/);
    mDispatcher->notifySwitch(&args);

    // InputDispatcher adds POLICY_FLAG_TRUSTED because the event went through InputListener
    args.policyFlags |= POLICY_FLAG_TRUSTED;
    mFakePolicy->assertNotifySwitchWasCalled(args);
}

// --- InputDispatcherTest SetInputWindowTest ---
static constexpr int32_t INJECT_EVENT_TIMEOUT = 500;
static constexpr nsecs_t DISPATCHING_TIMEOUT = seconds_to_nanoseconds(5);

class FakeApplicationHandle : public InputApplicationHandle {
public:
    FakeApplicationHandle() {}
    virtual ~FakeApplicationHandle() {}

    virtual bool updateInfo() {
        mInfo.dispatchingTimeout = DISPATCHING_TIMEOUT;
        return true;
    }
};

class FakeInputReceiver {
public:
    explicit FakeInputReceiver(const sp<InputChannel>& clientChannel, const std::string name)
          : mName(name) {
        mConsumer = std::make_unique<InputConsumer>(clientChannel);
    }

    InputEvent* consume() {
        uint32_t consumeSeq;
        InputEvent* event;

        std::chrono::time_point start = std::chrono::steady_clock::now();
        status_t status = WOULD_BLOCK;
        while (status == WOULD_BLOCK) {
            status = mConsumer->consume(&mEventFactory, true /*consumeBatches*/, -1, &consumeSeq,
                                        &event);
            std::chrono::duration elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > 100ms) {
                break;
            }
        }

        if (status == WOULD_BLOCK) {
            // Just means there's no event available.
            return nullptr;
        }

        if (status != OK) {
            ADD_FAILURE() << mName.c_str() << ": consumer consume should return OK.";
            return nullptr;
        }
        if (event == nullptr) {
            ADD_FAILURE() << "Consumed correctly, but received NULL event from consumer";
            return nullptr;
        }

        status = mConsumer->sendFinishedSignal(consumeSeq, true);
        if (status != OK) {
            ADD_FAILURE() << mName.c_str() << ": consumer sendFinishedSignal should return OK.";
        }
        return event;
    }

    void consumeEvent(int32_t expectedEventType, int32_t expectedAction, int32_t expectedDisplayId,
                      int32_t expectedFlags) {
        InputEvent* event = consume();

        ASSERT_NE(nullptr, event) << mName.c_str()
                                  << ": consumer should have returned non-NULL event.";
        ASSERT_EQ(expectedEventType, event->getType())
                << mName.c_str() << "expected " << inputEventTypeToString(expectedEventType)
                << " event, got " << inputEventTypeToString(event->getType()) << " event";

        EXPECT_EQ(expectedDisplayId, event->getDisplayId());

        switch (expectedEventType) {
            case AINPUT_EVENT_TYPE_KEY: {
                const KeyEvent& keyEvent = static_cast<const KeyEvent&>(*event);
                EXPECT_EQ(expectedAction, keyEvent.getAction());
                EXPECT_EQ(expectedFlags, keyEvent.getFlags());
                break;
            }
            case AINPUT_EVENT_TYPE_MOTION: {
                const MotionEvent& motionEvent = static_cast<const MotionEvent&>(*event);
                EXPECT_EQ(expectedAction, motionEvent.getAction());
                EXPECT_EQ(expectedFlags, motionEvent.getFlags());
                break;
            }
            case AINPUT_EVENT_TYPE_FOCUS: {
                FAIL() << "Use 'consumeFocusEvent' for FOCUS events";
            }
            default: {
                FAIL() << mName.c_str() << ": invalid event type: " << expectedEventType;
            }
        }
    }

    void consumeFocusEvent(bool hasFocus, bool inTouchMode) {
        InputEvent* event = consume();
        ASSERT_NE(nullptr, event) << mName.c_str()
                                  << ": consumer should have returned non-NULL event.";
        ASSERT_EQ(AINPUT_EVENT_TYPE_FOCUS, event->getType())
                << "Got " << inputEventTypeToString(event->getType())
                << " event instead of FOCUS event";

        ASSERT_EQ(ADISPLAY_ID_NONE, event->getDisplayId())
                << mName.c_str() << ": event displayId should always be NONE.";

        FocusEvent* focusEvent = static_cast<FocusEvent*>(event);
        EXPECT_EQ(hasFocus, focusEvent->getHasFocus());
        EXPECT_EQ(inTouchMode, focusEvent->getInTouchMode());
    }

    void assertNoEvents() {
        InputEvent* event = consume();
        ASSERT_EQ(nullptr, event)
                << mName.c_str()
                << ": should not have received any events, so consume() should return NULL";
    }

    sp<IBinder> getToken() { return mConsumer->getChannel()->getConnectionToken(); }

protected:
    std::unique_ptr<InputConsumer> mConsumer;
    PreallocatedInputEventFactory mEventFactory;

    std::string mName;
};

class FakeWindowHandle : public InputWindowHandle {
public:
    static const int32_t WIDTH = 600;
    static const int32_t HEIGHT = 800;

    FakeWindowHandle(const sp<InputApplicationHandle>& inputApplicationHandle,
                     const sp<InputDispatcher>& dispatcher, const std::string name,
                     int32_t displayId, sp<IBinder> token = nullptr)
          : mName(name) {
        if (token == nullptr) {
            sp<InputChannel> serverChannel, clientChannel;
            InputChannel::openInputChannelPair(name, serverChannel, clientChannel);
            mInputReceiver = std::make_unique<FakeInputReceiver>(clientChannel, name);
            dispatcher->registerInputChannel(serverChannel);
            token = serverChannel->getConnectionToken();
        }

        inputApplicationHandle->updateInfo();
        mInfo.applicationInfo = *inputApplicationHandle->getInfo();

        mInfo.token = token;
        mInfo.id = 0;
        mInfo.name = name;
        mInfo.layoutParamsFlags = 0;
        mInfo.layoutParamsType = InputWindowInfo::TYPE_APPLICATION;
        mInfo.dispatchingTimeout = DISPATCHING_TIMEOUT;
        mInfo.frameLeft = 0;
        mInfo.frameTop = 0;
        mInfo.frameRight = WIDTH;
        mInfo.frameBottom = HEIGHT;
        mInfo.globalScaleFactor = 1.0;
        mInfo.touchableRegion.clear();
        mInfo.addTouchableRegion(Rect(0, 0, WIDTH, HEIGHT));
        mInfo.visible = true;
        mInfo.canReceiveKeys = true;
        mInfo.hasFocus = false;
        mInfo.hasWallpaper = false;
        mInfo.paused = false;
        mInfo.ownerPid = INJECTOR_PID;
        mInfo.ownerUid = INJECTOR_UID;
        mInfo.inputFeatures = 0;
        mInfo.displayId = displayId;
    }

    virtual bool updateInfo() { return true; }

    void setFocus(bool hasFocus) { mInfo.hasFocus = hasFocus; }

    void setFrame(const Rect& frame) {
        mInfo.frameLeft = frame.left;
        mInfo.frameTop = frame.top;
        mInfo.frameRight = frame.right;
        mInfo.frameBottom = frame.bottom;
        mInfo.touchableRegion.clear();
        mInfo.addTouchableRegion(frame);
    }

    void setLayoutParamFlags(int32_t flags) { mInfo.layoutParamsFlags = flags; }

    void setId(int32_t id) { mInfo.id = id; }

    void setWindowScale(float xScale, float yScale) {
        mInfo.windowXScale = xScale;
        mInfo.windowYScale = yScale;
    }

    void consumeKeyDown(int32_t expectedDisplayId, int32_t expectedFlags = 0) {
        consumeEvent(AINPUT_EVENT_TYPE_KEY, AKEY_EVENT_ACTION_DOWN, expectedDisplayId,
                     expectedFlags);
    }

    void consumeMotionCancel(int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT,
            int32_t expectedFlags = 0) {
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_CANCEL, expectedDisplayId,
                     expectedFlags);
    }

    void consumeMotionMove(int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT,
            int32_t expectedFlags = 0) {
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_MOVE, expectedDisplayId,
                     expectedFlags);
    }

    void consumeMotionDown(int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT,
            int32_t expectedFlags = 0) {
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_DOWN, expectedDisplayId,
                     expectedFlags);
    }

    void consumeMotionPointerDown(int32_t pointerIdx,
            int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT, int32_t expectedFlags = 0) {
        int32_t action = AMOTION_EVENT_ACTION_POINTER_DOWN
                | (pointerIdx << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, action, expectedDisplayId, expectedFlags);
    }

    void consumeMotionPointerUp(int32_t pointerIdx, int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT,
            int32_t expectedFlags = 0) {
        int32_t action = AMOTION_EVENT_ACTION_POINTER_UP
                | (pointerIdx << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, action, expectedDisplayId, expectedFlags);
    }

    void consumeMotionUp(int32_t expectedDisplayId = ADISPLAY_ID_DEFAULT,
            int32_t expectedFlags = 0) {
        consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_UP, expectedDisplayId,
                     expectedFlags);
    }

    void consumeFocusEvent(bool hasFocus, bool inTouchMode = true) {
        ASSERT_NE(mInputReceiver, nullptr)
                << "Cannot consume events from a window with no receiver";
        mInputReceiver->consumeFocusEvent(hasFocus, inTouchMode);
    }

    void consumeEvent(int32_t expectedEventType, int32_t expectedAction, int32_t expectedDisplayId,
                      int32_t expectedFlags) {
        ASSERT_NE(mInputReceiver, nullptr) << "Invalid consume event on window with no receiver";
        mInputReceiver->consumeEvent(expectedEventType, expectedAction, expectedDisplayId,
                                     expectedFlags);
    }

    InputEvent* consume() {
        if (mInputReceiver == nullptr) {
            return nullptr;
        }
        return mInputReceiver->consume();
    }

    void assertNoEvents() {
        ASSERT_NE(mInputReceiver, nullptr)
                << "Call 'assertNoEvents' on a window with an InputReceiver";
        mInputReceiver->assertNoEvents();
    }

    sp<IBinder> getToken() { return mInfo.token; }

    const std::string& getName() { return mName; }

private:
    const std::string mName;
    std::unique_ptr<FakeInputReceiver> mInputReceiver;
};

static int32_t injectKeyDown(const sp<InputDispatcher>& dispatcher,
        int32_t displayId = ADISPLAY_ID_NONE) {
    KeyEvent event;
    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);

    // Define a valid key down event.
    event.initialize(InputEvent::nextId(), DEVICE_ID, AINPUT_SOURCE_KEYBOARD, displayId,
                     INVALID_HMAC, AKEY_EVENT_ACTION_DOWN, /* flags */ 0, AKEYCODE_A, KEY_A,
                     AMETA_NONE,
                     /* repeatCount */ 0, currentTime, currentTime);

    // Inject event until dispatch out.
    return dispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_WAIT_FOR_RESULT,
            INJECT_EVENT_TIMEOUT, POLICY_FLAG_FILTERED | POLICY_FLAG_PASS_TO_USER);
}

static int32_t injectMotionEvent(const sp<InputDispatcher>& dispatcher, int32_t action,
                                 int32_t source, int32_t displayId, int32_t x, int32_t y,
                                 int32_t xCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                 int32_t yCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION) {
    MotionEvent event;
    PointerProperties pointerProperties[1];
    PointerCoords pointerCoords[1];

    pointerProperties[0].clear();
    pointerProperties[0].id = 0;
    pointerProperties[0].toolType = AMOTION_EVENT_TOOL_TYPE_FINGER;

    pointerCoords[0].clear();
    pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_X, x);
    pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_Y, y);

    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    // Define a valid motion down event.
    event.initialize(InputEvent::nextId(), DEVICE_ID, source, displayId, INVALID_HMAC, action,
                     /* actionButton */ 0,
                     /* flags */ 0,
                     /* edgeFlags */ 0, AMETA_NONE, /* buttonState */ 0, MotionClassification::NONE,
                     /* xScale */ 1, /* yScale */ 1, /* xOffset */ 0, /* yOffset */ 0,
                     /* xPrecision */ 0, /* yPrecision */ 0, xCursorPosition, yCursorPosition,
                     currentTime, currentTime,
                     /*pointerCount*/ 1, pointerProperties, pointerCoords);

    // Inject event until dispatch out.
    return dispatcher->injectInputEvent(
            &event,
            INJECTOR_PID, INJECTOR_UID, INPUT_EVENT_INJECTION_SYNC_WAIT_FOR_RESULT,
            INJECT_EVENT_TIMEOUT, POLICY_FLAG_FILTERED | POLICY_FLAG_PASS_TO_USER);
}

static int32_t injectMotionDown(const sp<InputDispatcher>& dispatcher, int32_t source,
                                int32_t displayId, int32_t x = 100, int32_t y = 200) {
    return injectMotionEvent(dispatcher, AMOTION_EVENT_ACTION_DOWN, source, displayId, x, y);
}

static int32_t injectMotionUp(const sp<InputDispatcher>& dispatcher, int32_t source,
                              int32_t displayId, int32_t x = 100, int32_t y = 200) {
    return injectMotionEvent(dispatcher, AMOTION_EVENT_ACTION_UP, source, displayId, x, y);
}

static NotifyKeyArgs generateKeyArgs(int32_t action, int32_t displayId = ADISPLAY_ID_NONE) {
    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    // Define a valid key event.
    NotifyKeyArgs args(/* id */ 0, currentTime, DEVICE_ID, AINPUT_SOURCE_KEYBOARD, displayId,
                       POLICY_FLAG_PASS_TO_USER, action, /* flags */ 0, AKEYCODE_A, KEY_A,
                       AMETA_NONE, currentTime);

    return args;
}

static NotifyMotionArgs generateMotionArgs(int32_t action, int32_t source, int32_t displayId,
                                           const std::vector<PointF>& points) {
    size_t pointerCount = points.size();
    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_UP) {
        EXPECT_EQ(1U, pointerCount) << "Actions DOWN and UP can only contain a single pointer";
    }

    PointerProperties pointerProperties[pointerCount];
    PointerCoords pointerCoords[pointerCount];

    for (size_t i = 0; i < pointerCount; i++) {
        pointerProperties[i].clear();
        pointerProperties[i].id = i;
        pointerProperties[i].toolType = AMOTION_EVENT_TOOL_TYPE_FINGER;

        pointerCoords[i].clear();
        pointerCoords[i].setAxisValue(AMOTION_EVENT_AXIS_X, points[i].x);
        pointerCoords[i].setAxisValue(AMOTION_EVENT_AXIS_Y, points[i].y);
    }

    nsecs_t currentTime = systemTime(SYSTEM_TIME_MONOTONIC);
    // Define a valid motion event.
    NotifyMotionArgs args(/* id */ 0, currentTime, DEVICE_ID, source, displayId,
                          POLICY_FLAG_PASS_TO_USER, action, /* actionButton */ 0, /* flags */ 0,
                          AMETA_NONE, /* buttonState */ 0, MotionClassification::NONE,
                          AMOTION_EVENT_EDGE_FLAG_NONE, pointerCount, pointerProperties,
                          pointerCoords, /* xPrecision */ 0, /* yPrecision */ 0,
                          AMOTION_EVENT_INVALID_CURSOR_POSITION,
                          AMOTION_EVENT_INVALID_CURSOR_POSITION, currentTime, /* videoFrames */ {});

    return args;
}

static NotifyMotionArgs generateMotionArgs(int32_t action, int32_t source, int32_t displayId) {
    return generateMotionArgs(action, source, displayId, {PointF{100, 200}});
}

TEST_F(InputDispatcherTest, SetInputWindow_SingleWindowTouch) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window = new FakeWindowHandle(application, mDispatcher, "Fake Window",
            ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";

    // Window should receive motion event.
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);
}

// The foreground window should receive the first touch down event.
TEST_F(InputDispatcherTest, SetInputWindow_MultiWindowsTouch) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> windowTop = new FakeWindowHandle(application, mDispatcher, "Top",
            ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> windowSecond = new FakeWindowHandle(application, mDispatcher, "Second",
            ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowTop, windowSecond}}});
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";

    // Top window should receive the touch down event. Second window should not receive anything.
    windowTop->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    windowSecond->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetInputWindow_FocusedWindow) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> windowTop = new FakeWindowHandle(application, mDispatcher, "Top",
            ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> windowSecond = new FakeWindowHandle(application, mDispatcher, "Second",
            ADISPLAY_ID_DEFAULT);

    // Set focused application.
    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);

    // Display should have only one focused window
    windowSecond->setFocus(true);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowTop, windowSecond}}});

    windowSecond->consumeFocusEvent(true);
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";

    // Focused window should receive event.
    windowTop->assertNoEvents();
    windowSecond->consumeKeyDown(ADISPLAY_ID_NONE);
}

TEST_F(InputDispatcherTest, SetInputWindow_FocusPriority) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> windowTop = new FakeWindowHandle(application, mDispatcher, "Top",
            ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> windowSecond = new FakeWindowHandle(application, mDispatcher, "Second",
            ADISPLAY_ID_DEFAULT);

    // Set focused application.
    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);

    // Display has two focused windows. Add them to inputWindowsHandles in z-order (top most first)
    windowTop->setFocus(true);
    windowSecond->setFocus(true);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowTop, windowSecond}}});
    windowTop->consumeFocusEvent(true);
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";

    // Top focused window should receive event.
    windowTop->consumeKeyDown(ADISPLAY_ID_NONE);
    windowSecond->assertNoEvents();
}

TEST_F(InputDispatcherTest, SetInputWindow_InputWindowInfo) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();

    sp<FakeWindowHandle> windowTop = new FakeWindowHandle(application, mDispatcher, "Top",
            ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> windowSecond = new FakeWindowHandle(application, mDispatcher, "Second",
            ADISPLAY_ID_DEFAULT);

    // Set focused application.
    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);

    windowTop->setFocus(true);
    windowSecond->setFocus(true);
    // Release channel for window is no longer valid.
    windowTop->releaseChannel();
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowTop, windowSecond}}});
    windowSecond->consumeFocusEvent(true);

    // Test inject a key down, should dispatch to a valid window.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";

    // Top window is invalid, so it should not receive any input event.
    windowTop->assertNoEvents();
    windowSecond->consumeKeyDown(ADISPLAY_ID_NONE);
}

TEST_F(InputDispatcherTest, DispatchMouseEventsUnderCursor) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();

    sp<FakeWindowHandle> windowLeft =
            new FakeWindowHandle(application, mDispatcher, "Left", ADISPLAY_ID_DEFAULT);
    windowLeft->setFrame(Rect(0, 0, 600, 800));
    windowLeft->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL);
    sp<FakeWindowHandle> windowRight =
            new FakeWindowHandle(application, mDispatcher, "Right", ADISPLAY_ID_DEFAULT);
    windowRight->setFrame(Rect(600, 0, 1200, 800));
    windowRight->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL);

    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowLeft, windowRight}}});

    // Inject an event with coordinate in the area of right window, with mouse cursor in the area of
    // left window. This event should be dispatched to the left window.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED,
              injectMotionEvent(mDispatcher, AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_MOUSE,
                                ADISPLAY_ID_DEFAULT, 610, 400, 599, 400));
    windowLeft->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    windowRight->assertNoEvents();
}

TEST_F(InputDispatcherTest, NotifyDeviceReset_CancelsKeyStream) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);
    window->setFocus(true);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true);

    NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyKey(&keyArgs);

    // Window should receive key down event.
    window->consumeKeyDown(ADISPLAY_ID_DEFAULT);

    // When device reset happens, that key stream should be terminated with FLAG_CANCELED
    // on the app side.
    NotifyDeviceResetArgs args(10 /*id*/, 20 /*eventTime*/, DEVICE_ID);
    mDispatcher->notifyDeviceReset(&args);
    window->consumeEvent(AINPUT_EVENT_TYPE_KEY, AKEY_EVENT_ACTION_UP, ADISPLAY_ID_DEFAULT,
                         AKEY_EVENT_FLAG_CANCELED);
}

TEST_F(InputDispatcherTest, NotifyDeviceReset_CancelsMotionStream) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&motionArgs);

    // Window should receive motion down event.
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);

    // When device reset happens, that motion stream should be terminated with ACTION_CANCEL
    // on the app side.
    NotifyDeviceResetArgs args(10 /*id*/, 20 /*eventTime*/, DEVICE_ID);
    mDispatcher->notifyDeviceReset(&args);
    window->consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_CANCEL, ADISPLAY_ID_DEFAULT,
                         0 /*expectedFlags*/);
}

TEST_F(InputDispatcherTest, TransferTouchFocus_OnePointer) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();

    // Create a couple of windows
    sp<FakeWindowHandle> firstWindow = new FakeWindowHandle(application, mDispatcher,
            "First Window", ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> secondWindow = new FakeWindowHandle(application, mDispatcher,
            "Second Window", ADISPLAY_ID_DEFAULT);

    // Add the windows to the dispatcher
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {firstWindow, secondWindow}}});

    // Send down to the first window
    NotifyMotionArgs downMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&downMotionArgs);
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Transfer touch focus to the second window
    mDispatcher->transferTouchFocus(firstWindow->getToken(), secondWindow->getToken());
    // The first window gets cancel and the second gets down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown();

    // Send up event to the second window
    NotifyMotionArgs upMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_UP,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&upMotionArgs);
    // The first  window gets no events and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp();
}

TEST_F(InputDispatcherTest, TransferTouchFocus_TwoPointerNoSplitTouch) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();

    PointF touchPoint = {10, 10};

    // Create a couple of windows
    sp<FakeWindowHandle> firstWindow = new FakeWindowHandle(application, mDispatcher,
            "First Window", ADISPLAY_ID_DEFAULT);
    sp<FakeWindowHandle> secondWindow = new FakeWindowHandle(application, mDispatcher,
            "Second Window", ADISPLAY_ID_DEFAULT);

    // Add the windows to the dispatcher
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {firstWindow, secondWindow}}});

    // Send down to the first window
    NotifyMotionArgs downMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {touchPoint});
    mDispatcher->notifyMotion(&downMotionArgs);
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send pointer down to the first window
    NotifyMotionArgs pointerDownMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_POINTER_DOWN
            | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {touchPoint, touchPoint});
    mDispatcher->notifyMotion(&pointerDownMotionArgs);
    // Only the first window should get the pointer down event
    firstWindow->consumeMotionPointerDown(1);
    secondWindow->assertNoEvents();

    // Transfer touch focus to the second window
    mDispatcher->transferTouchFocus(firstWindow->getToken(), secondWindow->getToken());
    // The first window gets cancel and the second gets down and pointer down
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionDown();
    secondWindow->consumeMotionPointerDown(1);

    // Send pointer up to the second window
    NotifyMotionArgs pointerUpMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_POINTER_UP
            | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {touchPoint, touchPoint});
    mDispatcher->notifyMotion(&pointerUpMotionArgs);
    // The first window gets nothing and the second gets pointer up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionPointerUp(1);

    // Send up event to the second window
    NotifyMotionArgs upMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_UP,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&upMotionArgs);
    // The first window gets nothing and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp();
}

TEST_F(InputDispatcherTest, TransferTouchFocus_TwoPointersSplitTouch) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();

    // Create a non touch modal window that supports split touch
    sp<FakeWindowHandle> firstWindow = new FakeWindowHandle(application, mDispatcher,
            "First Window", ADISPLAY_ID_DEFAULT);
    firstWindow->setFrame(Rect(0, 0, 600, 400));
    firstWindow->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL
            | InputWindowInfo::FLAG_SPLIT_TOUCH);

    // Create a non touch modal window that supports split touch
    sp<FakeWindowHandle> secondWindow = new FakeWindowHandle(application, mDispatcher,
            "Second Window", ADISPLAY_ID_DEFAULT);
    secondWindow->setFrame(Rect(0, 400, 600, 800));
    secondWindow->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL
            | InputWindowInfo::FLAG_SPLIT_TOUCH);

    // Add the windows to the dispatcher
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {firstWindow, secondWindow}}});

    PointF pointInFirst = {300, 200};
    PointF pointInSecond = {300, 600};

    // Send down to the first window
    NotifyMotionArgs firstDownMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_DOWN,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {pointInFirst});
    mDispatcher->notifyMotion(&firstDownMotionArgs);
    // Only the first window should get the down event
    firstWindow->consumeMotionDown();
    secondWindow->assertNoEvents();

    // Send down to the second window
    NotifyMotionArgs secondDownMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_POINTER_DOWN
            | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {pointInFirst, pointInSecond});
    mDispatcher->notifyMotion(&secondDownMotionArgs);
    // The first window gets a move and the second a down
    firstWindow->consumeMotionMove();
    secondWindow->consumeMotionDown();

    // Transfer touch focus to the second window
    mDispatcher->transferTouchFocus(firstWindow->getToken(), secondWindow->getToken());
    // The first window gets cancel and the new gets pointer down (it already saw down)
    firstWindow->consumeMotionCancel();
    secondWindow->consumeMotionPointerDown(1);

    // Send pointer up to the second window
    NotifyMotionArgs pointerUpMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_POINTER_UP
            | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT),
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, {pointInFirst, pointInSecond});
    mDispatcher->notifyMotion(&pointerUpMotionArgs);
    // The first window gets nothing and the second gets pointer up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionPointerUp(1);

    // Send up event to the second window
    NotifyMotionArgs upMotionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_UP,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&upMotionArgs);
    // The first window gets nothing and the second gets up
    firstWindow->assertNoEvents();
    secondWindow->consumeMotionUp();
}

TEST_F(InputDispatcherTest, FocusedWindow_ReceivesFocusEventAndKeyEvent) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    window->setFocus(true);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    window->consumeFocusEvent(true);

    NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyKey(&keyArgs);

    // Window should receive key down event.
    window->consumeKeyDown(ADISPLAY_ID_DEFAULT);
}

TEST_F(InputDispatcherTest, UnfocusedWindow_DoesNotReceiveFocusEventOrKeyEvent) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyKey(&keyArgs);
    mDispatcher->waitForIdle();

    window->assertNoEvents();
}

// If a window is touchable, but does not have focus, it should receive motion events, but not keys
TEST_F(InputDispatcherTest, UnfocusedWindow_ReceivesMotionsButNotKeys) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    // Send key
    NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyKey(&keyArgs);
    // Send motion
    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&motionArgs);

    // Window should receive only the motion event
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    window->assertNoEvents(); // Key event or focus event will not be received
}

class FakeMonitorReceiver {
public:
    FakeMonitorReceiver(const sp<InputDispatcher>& dispatcher, const std::string name,
                        int32_t displayId, bool isGestureMonitor = false) {
        sp<InputChannel> serverChannel, clientChannel;
        InputChannel::openInputChannelPair(name, serverChannel, clientChannel);
        mInputReceiver = std::make_unique<FakeInputReceiver>(clientChannel, name);
        dispatcher->registerInputMonitor(serverChannel, displayId, isGestureMonitor);
    }

    sp<IBinder> getToken() { return mInputReceiver->getToken(); }

    void consumeKeyDown(int32_t expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver->consumeEvent(AINPUT_EVENT_TYPE_KEY, AKEY_EVENT_ACTION_DOWN,
                                     expectedDisplayId, expectedFlags);
    }

    void consumeMotionDown(int32_t expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver->consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_DOWN,
                                     expectedDisplayId, expectedFlags);
    }

    void consumeMotionUp(int32_t expectedDisplayId, int32_t expectedFlags = 0) {
        mInputReceiver->consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_UP,
                                     expectedDisplayId, expectedFlags);
    }

    void assertNoEvents() { mInputReceiver->assertNoEvents(); }

private:
    std::unique_ptr<FakeInputReceiver> mInputReceiver;
};

// Tests for gesture monitors
TEST_F(InputDispatcherTest, GestureMonitor_ReceivesMotionEvents) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    FakeMonitorReceiver monitor = FakeMonitorReceiver(mDispatcher, "GM_1", ADISPLAY_ID_DEFAULT,
                                                      true /*isGestureMonitor*/);

    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED,
              injectMotionDown(mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    monitor.consumeMotionDown(ADISPLAY_ID_DEFAULT);
}

TEST_F(InputDispatcherTest, GestureMonitor_DoesNotReceiveKeyEvents) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);
    window->setFocus(true);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true);

    FakeMonitorReceiver monitor = FakeMonitorReceiver(mDispatcher, "GM_1", ADISPLAY_ID_DEFAULT,
                                                      true /*isGestureMonitor*/);

    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher, ADISPLAY_ID_DEFAULT))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    window->consumeKeyDown(ADISPLAY_ID_DEFAULT);
    monitor.assertNoEvents();
}

TEST_F(InputDispatcherTest, GestureMonitor_CanPilferAfterWindowIsRemovedMidStream) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    FakeMonitorReceiver monitor = FakeMonitorReceiver(mDispatcher, "GM_1", ADISPLAY_ID_DEFAULT,
                                                      true /*isGestureMonitor*/);

    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED,
              injectMotionDown(mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    monitor.consumeMotionDown(ADISPLAY_ID_DEFAULT);

    window->releaseChannel();

    mDispatcher->pilferPointers(monitor.getToken());

    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED,
              injectMotionUp(mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    monitor.consumeMotionUp(ADISPLAY_ID_DEFAULT);
}

TEST_F(InputDispatcherTest, TestMoveEvent) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT);

    mDispatcher->notifyMotion(&motionArgs);
    // Window should receive motion down event.
    window->consumeMotionDown(ADISPLAY_ID_DEFAULT);

    motionArgs.action = AMOTION_EVENT_ACTION_MOVE;
    motionArgs.id += 1;
    motionArgs.eventTime = systemTime(SYSTEM_TIME_MONOTONIC);
    motionArgs.pointerCoords[0].setAxisValue(AMOTION_EVENT_AXIS_X,
                                             motionArgs.pointerCoords[0].getX() - 10);

    mDispatcher->notifyMotion(&motionArgs);
    window->consumeEvent(AINPUT_EVENT_TYPE_MOTION, AMOTION_EVENT_ACTION_MOVE, ADISPLAY_ID_DEFAULT,
                         0 /*expectedFlags*/);
}

/**
 * Dispatcher has touch mode enabled by default. Typically, the policy overrides that value to
 * the device default right away. In the test scenario, we check both the default value,
 * and the action of enabling / disabling.
 */
TEST_F(InputDispatcherTest, TouchModeState_IsSentToApps) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Test window", ADISPLAY_ID_DEFAULT);

    // Set focused application.
    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);
    window->setFocus(true);

    SCOPED_TRACE("Check default value of touch mode");
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true /*hasFocus*/, true /*inTouchMode*/);

    SCOPED_TRACE("Remove the window to trigger focus loss");
    window->setFocus(false);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(false /*hasFocus*/, true /*inTouchMode*/);

    SCOPED_TRACE("Disable touch mode");
    mDispatcher->setInTouchMode(false);
    window->setFocus(true);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true /*hasFocus*/, false /*inTouchMode*/);

    SCOPED_TRACE("Remove the window to trigger focus loss");
    window->setFocus(false);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(false /*hasFocus*/, false /*inTouchMode*/);

    SCOPED_TRACE("Enable touch mode again");
    mDispatcher->setInTouchMode(true);
    window->setFocus(true);
    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true /*hasFocus*/, true /*inTouchMode*/);

    window->assertNoEvents();
}

TEST_F(InputDispatcherTest, VerifyInputEvent_KeyEvent) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Test window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);
    window->setFocus(true);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});
    window->consumeFocusEvent(true /*hasFocus*/, true /*inTouchMode*/);

    NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN);
    mDispatcher->notifyKey(&keyArgs);

    InputEvent* event = window->consume();
    ASSERT_NE(event, nullptr);

    std::unique_ptr<VerifiedInputEvent> verified = mDispatcher->verifyInputEvent(*event);
    ASSERT_NE(verified, nullptr);
    ASSERT_EQ(verified->type, VerifiedInputEvent::Type::KEY);

    ASSERT_EQ(keyArgs.eventTime, verified->eventTimeNanos);
    ASSERT_EQ(keyArgs.deviceId, verified->deviceId);
    ASSERT_EQ(keyArgs.source, verified->source);
    ASSERT_EQ(keyArgs.displayId, verified->displayId);

    const VerifiedKeyEvent& verifiedKey = static_cast<const VerifiedKeyEvent&>(*verified);

    ASSERT_EQ(keyArgs.action, verifiedKey.action);
    ASSERT_EQ(keyArgs.downTime, verifiedKey.downTimeNanos);
    ASSERT_EQ(keyArgs.flags & VERIFIED_KEY_EVENT_FLAGS, verifiedKey.flags);
    ASSERT_EQ(keyArgs.keyCode, verifiedKey.keyCode);
    ASSERT_EQ(keyArgs.scanCode, verifiedKey.scanCode);
    ASSERT_EQ(keyArgs.metaState, verifiedKey.metaState);
    ASSERT_EQ(0, verifiedKey.repeatCount);
}

TEST_F(InputDispatcherTest, VerifyInputEvent_MotionEvent) {
    sp<FakeApplicationHandle> application = new FakeApplicationHandle();
    sp<FakeWindowHandle> window =
            new FakeWindowHandle(application, mDispatcher, "Test window", ADISPLAY_ID_DEFAULT);

    mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);

    mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {window}}});

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT);
    mDispatcher->notifyMotion(&motionArgs);

    InputEvent* event = window->consume();
    ASSERT_NE(event, nullptr);

    std::unique_ptr<VerifiedInputEvent> verified = mDispatcher->verifyInputEvent(*event);
    ASSERT_NE(verified, nullptr);
    ASSERT_EQ(verified->type, VerifiedInputEvent::Type::MOTION);

    EXPECT_EQ(motionArgs.eventTime, verified->eventTimeNanos);
    EXPECT_EQ(motionArgs.deviceId, verified->deviceId);
    EXPECT_EQ(motionArgs.source, verified->source);
    EXPECT_EQ(motionArgs.displayId, verified->displayId);

    const VerifiedMotionEvent& verifiedMotion = static_cast<const VerifiedMotionEvent&>(*verified);

    EXPECT_EQ(motionArgs.pointerCoords[0].getX(), verifiedMotion.rawX);
    EXPECT_EQ(motionArgs.pointerCoords[0].getY(), verifiedMotion.rawY);
    EXPECT_EQ(motionArgs.action & AMOTION_EVENT_ACTION_MASK, verifiedMotion.actionMasked);
    EXPECT_EQ(motionArgs.downTime, verifiedMotion.downTimeNanos);
    EXPECT_EQ(motionArgs.flags & VERIFIED_MOTION_EVENT_FLAGS, verifiedMotion.flags);
    EXPECT_EQ(motionArgs.metaState, verifiedMotion.metaState);
    EXPECT_EQ(motionArgs.buttonState, verifiedMotion.buttonState);
}

class InputDispatcherKeyRepeatTest : public InputDispatcherTest {
protected:
    static constexpr nsecs_t KEY_REPEAT_TIMEOUT = 40 * 1000000; // 40 ms
    static constexpr nsecs_t KEY_REPEAT_DELAY = 40 * 1000000;   // 40 ms

    sp<FakeApplicationHandle> mApp;
    sp<FakeWindowHandle> mWindow;

    virtual void SetUp() override {
        mFakePolicy = new FakeInputDispatcherPolicy();
        mFakePolicy->setKeyRepeatConfiguration(KEY_REPEAT_TIMEOUT, KEY_REPEAT_DELAY);
        mDispatcher = new InputDispatcher(mFakePolicy);
        mDispatcher->setInputDispatchMode(/*enabled*/ true, /*frozen*/ false);
        ASSERT_EQ(OK, mDispatcher->start());

        setUpWindow();
    }

    void setUpWindow() {
        mApp = new FakeApplicationHandle();
        mWindow = new FakeWindowHandle(mApp, mDispatcher, "Fake Window", ADISPLAY_ID_DEFAULT);

        mWindow->setFocus(true);
        mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {mWindow}}});

        mWindow->consumeFocusEvent(true);
    }

    void sendAndConsumeKeyDown() {
        NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN, ADISPLAY_ID_DEFAULT);
        keyArgs.policyFlags |= POLICY_FLAG_TRUSTED; // Otherwise it won't generate repeat event
        mDispatcher->notifyKey(&keyArgs);

        // Window should receive key down event.
        mWindow->consumeKeyDown(ADISPLAY_ID_DEFAULT);
    }

    void expectKeyRepeatOnce(int32_t repeatCount) {
        SCOPED_TRACE(StringPrintf("Checking event with repeat count %" PRId32, repeatCount));
        InputEvent* repeatEvent = mWindow->consume();
        ASSERT_NE(nullptr, repeatEvent);

        uint32_t eventType = repeatEvent->getType();
        ASSERT_EQ(AINPUT_EVENT_TYPE_KEY, eventType);

        KeyEvent* repeatKeyEvent = static_cast<KeyEvent*>(repeatEvent);
        uint32_t eventAction = repeatKeyEvent->getAction();
        EXPECT_EQ(AKEY_EVENT_ACTION_DOWN, eventAction);
        EXPECT_EQ(repeatCount, repeatKeyEvent->getRepeatCount());
    }

    void sendAndConsumeKeyUp() {
        NotifyKeyArgs keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_UP, ADISPLAY_ID_DEFAULT);
        keyArgs.policyFlags |= POLICY_FLAG_TRUSTED; // Unless it won't generate repeat event
        mDispatcher->notifyKey(&keyArgs);

        // Window should receive key down event.
        mWindow->consumeEvent(AINPUT_EVENT_TYPE_KEY, AKEY_EVENT_ACTION_UP, ADISPLAY_ID_DEFAULT,
                              0 /*expectedFlags*/);
    }
};

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_ReceivesKeyRepeat) {
    sendAndConsumeKeyDown();
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        expectKeyRepeatOnce(repeatCount);
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_StopsKeyRepeatAfterUp) {
    sendAndConsumeKeyDown();
    expectKeyRepeatOnce(1 /*repeatCount*/);
    sendAndConsumeKeyUp();
    mWindow->assertNoEvents();
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_RepeatKeyEventsUseEventIdFromInputDispatcher) {
    sendAndConsumeKeyDown();
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        InputEvent* repeatEvent = mWindow->consume();
        ASSERT_NE(nullptr, repeatEvent) << "Didn't receive event with repeat count " << repeatCount;
        EXPECT_EQ(IdGenerator::Source::INPUT_DISPATCHER,
                  IdGenerator::getSource(repeatEvent->getId()));
    }
}

TEST_F(InputDispatcherKeyRepeatTest, FocusedWindow_RepeatKeyEventsUseUniqueEventId) {
    sendAndConsumeKeyDown();

    std::unordered_set<int32_t> idSet;
    for (int32_t repeatCount = 1; repeatCount <= 10; ++repeatCount) {
        InputEvent* repeatEvent = mWindow->consume();
        ASSERT_NE(nullptr, repeatEvent) << "Didn't receive event with repeat count " << repeatCount;
        int32_t id = repeatEvent->getId();
        EXPECT_EQ(idSet.end(), idSet.find(id));
        idSet.insert(id);
    }
}

/* Test InputDispatcher for MultiDisplay */
class InputDispatcherFocusOnTwoDisplaysTest : public InputDispatcherTest {
public:
    static constexpr int32_t SECOND_DISPLAY_ID = 1;
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        application1 = new FakeApplicationHandle();
        windowInPrimary = new FakeWindowHandle(application1, mDispatcher, "D_1",
                ADISPLAY_ID_DEFAULT);

        // Set focus window for primary display, but focused display would be second one.
        mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application1);
        windowInPrimary->setFocus(true);
        mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {windowInPrimary}}});
        windowInPrimary->consumeFocusEvent(true);

        application2 = new FakeApplicationHandle();
        windowInSecondary = new FakeWindowHandle(application2, mDispatcher, "D_2",
                SECOND_DISPLAY_ID);
        // Set focus to second display window.
        // Set focus display to second one.
        mDispatcher->setFocusedDisplay(SECOND_DISPLAY_ID);
        // Set focus window for second display.
        mDispatcher->setFocusedApplication(SECOND_DISPLAY_ID, application2);
        windowInSecondary->setFocus(true);
        mDispatcher->setInputWindows({{SECOND_DISPLAY_ID, {windowInSecondary}}});
        windowInSecondary->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();

        application1.clear();
        windowInPrimary.clear();
        application2.clear();
        windowInSecondary.clear();
    }

protected:
    sp<FakeApplicationHandle> application1;
    sp<FakeWindowHandle> windowInPrimary;
    sp<FakeApplicationHandle> application2;
    sp<FakeWindowHandle> windowInSecondary;
};

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, SetInputWindow_MultiDisplayTouch) {
    // Test touch down on primary display.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    windowInSecondary->assertNoEvents();

    // Test touch down on second display.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);
}

TEST_F(InputDispatcherFocusOnTwoDisplaysTest, SetInputWindow_MultiDisplayFocus) {
    // Test inject a key down with display id specified.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher, ADISPLAY_ID_DEFAULT))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->consumeKeyDown(ADISPLAY_ID_DEFAULT);
    windowInSecondary->assertNoEvents();

    // Test inject a key down without display id specified.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeKeyDown(ADISPLAY_ID_NONE);

    // Remove all windows in secondary display.
    mDispatcher->setInputWindows({{SECOND_DISPLAY_ID, {}}});

    // Expect old focus should receive a cancel event.
    windowInSecondary->consumeEvent(AINPUT_EVENT_TYPE_KEY, AKEY_EVENT_ACTION_UP, ADISPLAY_ID_NONE,
                                    AKEY_EVENT_FLAG_CANCELED);

    // Test inject a key down, should timeout because of no target window.
    ASSERT_EQ(INPUT_EVENT_INJECTION_TIMED_OUT, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_TIMED_OUT";
    windowInPrimary->assertNoEvents();
    windowInSecondary->consumeFocusEvent(false);
    windowInSecondary->assertNoEvents();
}

// Test per-display input monitors for motion event.
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, MonitorMotionEvent_MultiDisplay) {
    FakeMonitorReceiver monitorInPrimary =
            FakeMonitorReceiver(mDispatcher, "M_1", ADISPLAY_ID_DEFAULT);
    FakeMonitorReceiver monitorInSecondary =
            FakeMonitorReceiver(mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // Test touch down on primary display.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->consumeMotionDown(ADISPLAY_ID_DEFAULT);
    monitorInPrimary.consumeMotionDown(ADISPLAY_ID_DEFAULT);
    windowInSecondary->assertNoEvents();
    monitorInSecondary.assertNoEvents();

    // Test touch down on second display.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, SECOND_DISPLAY_ID))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeMotionDown(SECOND_DISPLAY_ID);
    monitorInSecondary.consumeMotionDown(SECOND_DISPLAY_ID);

    // Test inject a non-pointer motion event.
    // If specific a display, it will dispatch to the focused window of particular display,
    // or it will dispatch to the focused window of focused display.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
        AINPUT_SOURCE_TRACKBALL, ADISPLAY_ID_NONE))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeMotionDown(ADISPLAY_ID_NONE);
    monitorInSecondary.consumeMotionDown(ADISPLAY_ID_NONE);
}

// Test per-display input monitors for key event.
TEST_F(InputDispatcherFocusOnTwoDisplaysTest, MonitorKeyEvent_MultiDisplay) {
    //Input monitor per display.
    FakeMonitorReceiver monitorInPrimary =
            FakeMonitorReceiver(mDispatcher, "M_1", ADISPLAY_ID_DEFAULT);
    FakeMonitorReceiver monitorInSecondary =
            FakeMonitorReceiver(mDispatcher, "M_2", SECOND_DISPLAY_ID);

    // Test inject a key down.
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    windowInPrimary->assertNoEvents();
    monitorInPrimary.assertNoEvents();
    windowInSecondary->consumeKeyDown(ADISPLAY_ID_NONE);
    monitorInSecondary.consumeKeyDown(ADISPLAY_ID_NONE);
}

class InputFilterTest : public InputDispatcherTest {
protected:
    static constexpr int32_t SECOND_DISPLAY_ID = 1;

    void testNotifyMotion(int32_t displayId, bool expectToBeFiltered) {
        NotifyMotionArgs motionArgs;

        motionArgs = generateMotionArgs(
                AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN, displayId);
        mDispatcher->notifyMotion(&motionArgs);
        motionArgs = generateMotionArgs(
                AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN, displayId);
        mDispatcher->notifyMotion(&motionArgs);
        ASSERT_TRUE(mDispatcher->waitForIdle());
        if (expectToBeFiltered) {
            mFakePolicy->assertFilterInputEventWasCalled(motionArgs);
        } else {
            mFakePolicy->assertFilterInputEventWasNotCalled();
        }
    }

    void testNotifyKey(bool expectToBeFiltered) {
        NotifyKeyArgs keyArgs;

        keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_DOWN);
        mDispatcher->notifyKey(&keyArgs);
        keyArgs = generateKeyArgs(AKEY_EVENT_ACTION_UP);
        mDispatcher->notifyKey(&keyArgs);
        ASSERT_TRUE(mDispatcher->waitForIdle());

        if (expectToBeFiltered) {
            mFakePolicy->assertFilterInputEventWasCalled(keyArgs);
        } else {
            mFakePolicy->assertFilterInputEventWasNotCalled();
        }
    }
};

// Test InputFilter for MotionEvent
TEST_F(InputFilterTest, MotionEvent_InputFilter) {
    // Since the InputFilter is disabled by default, check if touch events aren't filtered.
    testNotifyMotion(ADISPLAY_ID_DEFAULT, /*expectToBeFiltered*/ false);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered*/ false);

    // Enable InputFilter
    mDispatcher->setInputFilterEnabled(true);
    // Test touch on both primary and second display, and check if both events are filtered.
    testNotifyMotion(ADISPLAY_ID_DEFAULT, /*expectToBeFiltered*/ true);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered*/ true);

    // Disable InputFilter
    mDispatcher->setInputFilterEnabled(false);
    // Test touch on both primary and second display, and check if both events aren't filtered.
    testNotifyMotion(ADISPLAY_ID_DEFAULT, /*expectToBeFiltered*/ false);
    testNotifyMotion(SECOND_DISPLAY_ID, /*expectToBeFiltered*/ false);
}

// Test InputFilter for KeyEvent
TEST_F(InputFilterTest, KeyEvent_InputFilter) {
    // Since the InputFilter is disabled by default, check if key event aren't filtered.
    testNotifyKey(/*expectToBeFiltered*/ false);

    // Enable InputFilter
    mDispatcher->setInputFilterEnabled(true);
    // Send a key event, and check if it is filtered.
    testNotifyKey(/*expectToBeFiltered*/ true);

    // Disable InputFilter
    mDispatcher->setInputFilterEnabled(false);
    // Send a key event, and check if it isn't filtered.
    testNotifyKey(/*expectToBeFiltered*/ false);
}

class InputDispatcherOnPointerDownOutsideFocus : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        sp<FakeApplicationHandle> application = new FakeApplicationHandle();
        mUnfocusedWindow = new FakeWindowHandle(application, mDispatcher, "Top",
                ADISPLAY_ID_DEFAULT);
        mUnfocusedWindow->setFrame(Rect(0, 0, 30, 30));
        // Adding FLAG_NOT_TOUCH_MODAL to ensure taps outside this window are not sent to this
        // window.
        mUnfocusedWindow->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL);

        mFocusedWindow =
                new FakeWindowHandle(application, mDispatcher, "Second", ADISPLAY_ID_DEFAULT);
        mFocusedWindow->setFrame(Rect(50, 50, 100, 100));
        mFocusedWindow->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL);
        mFocusedWindowTouchPoint = 60;

        // Set focused application.
        mDispatcher->setFocusedApplication(ADISPLAY_ID_DEFAULT, application);
        mFocusedWindow->setFocus(true);

        // Expect one focus window exist in display.
        mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {mUnfocusedWindow, mFocusedWindow}}});
        mFocusedWindow->consumeFocusEvent(true);
    }

    virtual void TearDown() override {
        InputDispatcherTest::TearDown();

        mUnfocusedWindow.clear();
        mFocusedWindow.clear();
    }

protected:
    sp<FakeWindowHandle> mUnfocusedWindow;
    sp<FakeWindowHandle> mFocusedWindow;
    int32_t mFocusedWindowTouchPoint;
};

// Have two windows, one with focus. Inject MotionEvent with source TOUCHSCREEN and action
// DOWN on the window that doesn't have focus. Ensure the window that didn't have focus received
// the onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_Success) {
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT, 20, 20))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    mUnfocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownEquals(mUnfocusedWindow->getToken());
}

// Have two windows, one with focus. Inject MotionEvent with source TRACKBALL and action
// DOWN on the window that doesn't have focus. Ensure no window received the
// onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_NonPointerSource) {
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectMotionDown(mDispatcher,
            AINPUT_SOURCE_TRACKBALL, ADISPLAY_ID_DEFAULT, 20, 20))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    mFocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// Have two windows, one with focus. Inject KeyEvent with action DOWN on the window that doesn't
// have focus. Ensure no window received the onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus, OnPointerDownOutsideFocus_NonMotionFailure) {
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED, injectKeyDown(mDispatcher, ADISPLAY_ID_DEFAULT))
            << "Inject key event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    mFocusedWindow->consumeKeyDown(ADISPLAY_ID_DEFAULT);

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// Have two windows, one with focus. Inject MotionEvent with source TOUCHSCREEN and action
// DOWN on the window that already has focus. Ensure no window received the
// onPointerDownOutsideFocus callback.
TEST_F(InputDispatcherOnPointerDownOutsideFocus,
        OnPointerDownOutsideFocus_OnAlreadyFocusedWindow) {
    ASSERT_EQ(INPUT_EVENT_INJECTION_SUCCEEDED,
              injectMotionDown(mDispatcher, AINPUT_SOURCE_TOUCHSCREEN, ADISPLAY_ID_DEFAULT,
                               mFocusedWindowTouchPoint, mFocusedWindowTouchPoint))
            << "Inject motion event should return INPUT_EVENT_INJECTION_SUCCEEDED";
    mFocusedWindow->consumeMotionDown();

    ASSERT_TRUE(mDispatcher->waitForIdle());
    mFakePolicy->assertOnPointerDownWasNotCalled();
}

// These tests ensures we can send touch events to a single client when there are multiple input
// windows that point to the same client token.
class InputDispatcherMultiWindowSameTokenTests : public InputDispatcherTest {
    virtual void SetUp() override {
        InputDispatcherTest::SetUp();

        sp<FakeApplicationHandle> application = new FakeApplicationHandle();
        mWindow1 = new FakeWindowHandle(application, mDispatcher, "Fake Window 1",
                                        ADISPLAY_ID_DEFAULT);
        // Adding FLAG_NOT_TOUCH_MODAL otherwise all taps will go to the top most window.
        // We also need FLAG_SPLIT_TOUCH or we won't be able to get touches for both windows.
        mWindow1->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL |
                                      InputWindowInfo::FLAG_SPLIT_TOUCH);
        mWindow1->setId(0);
        mWindow1->setFrame(Rect(0, 0, 100, 100));

        mWindow2 = new FakeWindowHandle(application, mDispatcher, "Fake Window 2",
                                        ADISPLAY_ID_DEFAULT, mWindow1->getToken());
        mWindow2->setLayoutParamFlags(InputWindowInfo::FLAG_NOT_TOUCH_MODAL |
                                      InputWindowInfo::FLAG_SPLIT_TOUCH);
        mWindow2->setId(1);
        mWindow2->setFrame(Rect(100, 100, 200, 200));

        mDispatcher->setInputWindows({{ADISPLAY_ID_DEFAULT, {mWindow1, mWindow2}}});
    }

protected:
    sp<FakeWindowHandle> mWindow1;
    sp<FakeWindowHandle> mWindow2;

    // Helper function to convert the point from screen coordinates into the window's space
    static PointF getPointInWindow(const InputWindowInfo* windowInfo, const PointF& point) {
        float x = windowInfo->windowXScale * (point.x - windowInfo->frameLeft);
        float y = windowInfo->windowYScale * (point.y - windowInfo->frameTop);
        return {x, y};
    }

    void consumeMotionEvent(const sp<FakeWindowHandle>& window, int32_t expectedAction,
                            const std::vector<PointF>& points) {
        const std::string name = window->getName();
        InputEvent* event = window->consume();

        ASSERT_NE(nullptr, event) << name.c_str()
                                  << ": consumer should have returned non-NULL event.";

        ASSERT_EQ(AINPUT_EVENT_TYPE_MOTION, event->getType())
                << name.c_str() << "expected " << inputEventTypeToString(AINPUT_EVENT_TYPE_MOTION)
                << " event, got " << inputEventTypeToString(event->getType()) << " event";

        const MotionEvent& motionEvent = static_cast<const MotionEvent&>(*event);
        EXPECT_EQ(expectedAction, motionEvent.getAction());

        for (size_t i = 0; i < points.size(); i++) {
            float expectedX = points[i].x;
            float expectedY = points[i].y;

            EXPECT_EQ(expectedX, motionEvent.getX(i))
                    << "expected " << expectedX << " for x[" << i << "] coord of " << name.c_str()
                    << ", got " << motionEvent.getX(i);
            EXPECT_EQ(expectedY, motionEvent.getY(i))
                    << "expected " << expectedY << " for y[" << i << "] coord of " << name.c_str()
                    << ", got " << motionEvent.getY(i);
        }
    }
};

TEST_F(InputDispatcherMultiWindowSameTokenTests, SingleTouchSameScale) {
    // Touch Window 1
    PointF touchedPoint = {10, 10};
    PointF expectedPoint = getPointInWindow(mWindow1->getInfo(), touchedPoint);

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, {expectedPoint});

    // Release touch on Window 1
    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);
    // consume the UP event
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_UP, {expectedPoint});

    // Touch Window 2
    touchedPoint = {150, 150};
    expectedPoint = getPointInWindow(mWindow2->getInfo(), touchedPoint);

    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);

    // Consuming from window1 since it's the window that has the InputReceiver
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, {expectedPoint});
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, SingleTouchDifferentScale) {
    mWindow2->setWindowScale(0.5f, 0.5f);

    // Touch Window 1
    PointF touchedPoint = {10, 10};
    PointF expectedPoint = getPointInWindow(mWindow1->getInfo(), touchedPoint);

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, {expectedPoint});

    // Release touch on Window 1
    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_UP, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);
    // consume the UP event
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_UP, {expectedPoint});

    // Touch Window 2
    touchedPoint = {150, 150};
    expectedPoint = getPointInWindow(mWindow2->getInfo(), touchedPoint);

    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, {touchedPoint});
    mDispatcher->notifyMotion(&motionArgs);

    // Consuming from window1 since it's the window that has the InputReceiver
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, {expectedPoint});
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleTouchDifferentScale) {
    mWindow2->setWindowScale(0.5f, 0.5f);

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, expectedPoints);

    // Touch Window 2
    int32_t actionPointerDown =
            AMOTION_EVENT_ACTION_POINTER_DOWN + (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    touchedPoints.emplace_back(PointF{150, 150});
    expectedPoints.emplace_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));

    motionArgs = generateMotionArgs(actionPointerDown, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);

    // Consuming from window1 since it's the window that has the InputReceiver
    consumeMotionEvent(mWindow1, actionPointerDown, expectedPoints);
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleTouchMoveDifferentScale) {
    mWindow2->setWindowScale(0.5f, 0.5f);

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, expectedPoints);

    // Touch Window 2
    int32_t actionPointerDown =
            AMOTION_EVENT_ACTION_POINTER_DOWN + (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    touchedPoints.emplace_back(PointF{150, 150});
    expectedPoints.emplace_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));

    motionArgs = generateMotionArgs(actionPointerDown, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);

    // Consuming from window1 since it's the window that has the InputReceiver
    consumeMotionEvent(mWindow1, actionPointerDown, expectedPoints);

    // Move both windows
    touchedPoints = {{20, 20}, {175, 175}};
    expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0]),
                      getPointInWindow(mWindow2->getInfo(), touchedPoints[1])};

    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);

    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_MOVE, expectedPoints);
}

TEST_F(InputDispatcherMultiWindowSameTokenTests, MultipleWindowsFirstTouchWithScale) {
    mWindow1->setWindowScale(0.5f, 0.5f);

    // Touch Window 1
    std::vector<PointF> touchedPoints = {PointF{10, 10}};
    std::vector<PointF> expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0])};

    NotifyMotionArgs motionArgs =
            generateMotionArgs(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_TOUCHSCREEN,
                               ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);
    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_DOWN, expectedPoints);

    // Touch Window 2
    int32_t actionPointerDown =
            AMOTION_EVENT_ACTION_POINTER_DOWN + (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    touchedPoints.emplace_back(PointF{150, 150});
    expectedPoints.emplace_back(getPointInWindow(mWindow2->getInfo(), touchedPoints[1]));

    motionArgs = generateMotionArgs(actionPointerDown, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);

    // Consuming from window1 since it's the window that has the InputReceiver
    consumeMotionEvent(mWindow1, actionPointerDown, expectedPoints);

    // Move both windows
    touchedPoints = {{20, 20}, {175, 175}};
    expectedPoints = {getPointInWindow(mWindow1->getInfo(), touchedPoints[0]),
                      getPointInWindow(mWindow2->getInfo(), touchedPoints[1])};

    motionArgs = generateMotionArgs(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN,
                                    ADISPLAY_ID_DEFAULT, touchedPoints);
    mDispatcher->notifyMotion(&motionArgs);

    consumeMotionEvent(mWindow1, AMOTION_EVENT_ACTION_MOVE, expectedPoints);
}

} // namespace android::inputdispatcher
