/*
 * Copyright 2018 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "Scheduler"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "Scheduler.h"

#include <android-base/stringprintf.h>
#include <android/hardware/configstore/1.0/ISurfaceFlingerConfigs.h>
#include <android/hardware/configstore/1.1/ISurfaceFlingerConfigs.h>
#include <configstore/Utils.h>
#include <cutils/properties.h>
#include <input/InputWindow.h>
#include <system/window.h>
#include <ui/DisplayStatInfo.h>
#include <utils/Timers.h>
#include <utils/Trace.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>

#include "../Layer.h"
#include "DispSync.h"
#include "DispSyncSource.h"
#include "EventControlThread.h"
#include "EventThread.h"
#include "InjectVSyncSource.h"
#include "OneShotTimer.h"
#include "SchedulerUtils.h"
#include "SurfaceFlingerProperties.h"

#define RETURN_IF_INVALID_HANDLE(handle, ...)                        \
    do {                                                             \
        if (mConnections.count(handle) == 0) {                       \
            ALOGE("Invalid connection handle %" PRIuPTR, handle.id); \
            return __VA_ARGS__;                                      \
        }                                                            \
    } while (false)

namespace android {

Scheduler::Scheduler(impl::EventControlThread::SetVSyncEnabledFunction function,
                     const scheduler::RefreshRateConfigs& refreshRateConfig,
                     ISchedulerCallback& schedulerCallback)
      : mPrimaryDispSync(new impl::DispSync("SchedulerDispSync",
                                            sysprop::running_without_sync_framework(true))),
        mEventControlThread(new impl::EventControlThread(std::move(function))),
        mSupportKernelTimer(sysprop::support_kernel_idle_timer(false)),
        mSchedulerCallback(schedulerCallback),
        mRefreshRateConfigs(refreshRateConfig) {
    using namespace sysprop;

    if (property_get_bool("debug.sf.use_smart_90_for_video", 0) || use_smart_90_for_video(false)) {
        mLayerHistory.emplace();
    }

    const int setIdleTimerMs = property_get_int32("debug.sf.set_idle_timer_ms", 0);

    if (const auto millis = setIdleTimerMs ? setIdleTimerMs : set_idle_timer_ms(0); millis > 0) {
        const auto callback = mSupportKernelTimer ? &Scheduler::kernelIdleTimerCallback
                                                  : &Scheduler::idleTimerCallback;

        mIdleTimer.emplace(
                std::chrono::milliseconds(millis),
                [this, callback] { std::invoke(callback, this, TimerState::Reset); },
                [this, callback] { std::invoke(callback, this, TimerState::Expired); });
        mIdleTimer->start();
    }

    if (const int64_t millis = set_touch_timer_ms(0); millis > 0) {
        // Touch events are coming to SF every 100ms, so the timer needs to be higher than that
        mTouchTimer.emplace(
                std::chrono::milliseconds(millis),
                [this] { touchTimerCallback(TimerState::Reset); },
                [this] { touchTimerCallback(TimerState::Expired); });
        mTouchTimer->start();
    }

    if (const int64_t millis = set_display_power_timer_ms(0); millis > 0) {
        mDisplayPowerTimer.emplace(
                std::chrono::milliseconds(millis),
                [this] { displayPowerTimerCallback(TimerState::Reset); },
                [this] { displayPowerTimerCallback(TimerState::Expired); });
        mDisplayPowerTimer->start();
    }
}

Scheduler::Scheduler(std::unique_ptr<DispSync> primaryDispSync,
                     std::unique_ptr<EventControlThread> eventControlThread,
                     const scheduler::RefreshRateConfigs& configs,
                     ISchedulerCallback& schedulerCallback)
      : mPrimaryDispSync(std::move(primaryDispSync)),
        mEventControlThread(std::move(eventControlThread)),
        mSupportKernelTimer(false),
        mSchedulerCallback(schedulerCallback),
        mRefreshRateConfigs(configs) {}

Scheduler::~Scheduler() {
    // Ensure the OneShotTimer threads are joined before we start destroying state.
    mDisplayPowerTimer.reset();
    mTouchTimer.reset();
    mIdleTimer.reset();
}

DispSync& Scheduler::getPrimaryDispSync() {
    return *mPrimaryDispSync;
}

std::unique_ptr<VSyncSource> Scheduler::makePrimaryDispSyncSource(
        const char* name, nsecs_t phaseOffsetNs, nsecs_t offsetThresholdForNextVsync) {
    return std::make_unique<DispSyncSource>(mPrimaryDispSync.get(), phaseOffsetNs,
                                            offsetThresholdForNextVsync, true /* traceVsync */,
                                            name);
}

Scheduler::ConnectionHandle Scheduler::createConnection(
        const char* connectionName, nsecs_t phaseOffsetNs, nsecs_t offsetThresholdForNextVsync,
        impl::EventThread::InterceptVSyncsCallback interceptCallback) {
    auto vsyncSource =
            makePrimaryDispSyncSource(connectionName, phaseOffsetNs, offsetThresholdForNextVsync);
    auto eventThread = std::make_unique<impl::EventThread>(std::move(vsyncSource),
                                                           std::move(interceptCallback));
    return createConnection(std::move(eventThread));
}

Scheduler::ConnectionHandle Scheduler::createConnection(std::unique_ptr<EventThread> eventThread) {
    const ConnectionHandle handle = ConnectionHandle{mNextConnectionHandleId++};
    ALOGV("Creating a connection handle with ID %" PRIuPTR, handle.id);

    auto connection =
            createConnectionInternal(eventThread.get(), ISurfaceComposer::eConfigChangedSuppress);

    mConnections.emplace(handle, Connection{connection, std::move(eventThread)});
    return handle;
}

sp<EventThreadConnection> Scheduler::createConnectionInternal(
        EventThread* eventThread, ISurfaceComposer::ConfigChanged configChanged) {
    return eventThread->createEventConnection([&] { resync(); }, configChanged);
}

sp<IDisplayEventConnection> Scheduler::createDisplayEventConnection(
        ConnectionHandle handle, ISurfaceComposer::ConfigChanged configChanged) {
    RETURN_IF_INVALID_HANDLE(handle, nullptr);
    return createConnectionInternal(mConnections[handle].thread.get(), configChanged);
}

sp<EventThreadConnection> Scheduler::getEventConnection(ConnectionHandle handle) {
    RETURN_IF_INVALID_HANDLE(handle, nullptr);
    return mConnections[handle].connection;
}

void Scheduler::onHotplugReceived(ConnectionHandle handle, PhysicalDisplayId displayId,
                                  bool connected) {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections[handle].thread->onHotplugReceived(displayId, connected);
}

void Scheduler::onScreenAcquired(ConnectionHandle handle) {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections[handle].thread->onScreenAcquired();
}

void Scheduler::onScreenReleased(ConnectionHandle handle) {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections[handle].thread->onScreenReleased();
}

void Scheduler::onConfigChanged(ConnectionHandle handle, PhysicalDisplayId displayId,
                                HwcConfigIndexType configId) {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections[handle].thread->onConfigChanged(displayId, configId);
}

void Scheduler::dump(ConnectionHandle handle, std::string& result) const {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections.at(handle).thread->dump(result);
}

void Scheduler::setPhaseOffset(ConnectionHandle handle, nsecs_t phaseOffset) {
    RETURN_IF_INVALID_HANDLE(handle);
    mConnections[handle].thread->setPhaseOffset(phaseOffset);
}

void Scheduler::getDisplayStatInfo(DisplayStatInfo* stats) {
    stats->vsyncTime = mPrimaryDispSync->computeNextRefresh(0);
    stats->vsyncPeriod = mPrimaryDispSync->getPeriod();
}

Scheduler::ConnectionHandle Scheduler::enableVSyncInjection(bool enable) {
    if (mInjectVSyncs == enable) {
        return {};
    }

    ALOGV("%s VSYNC injection", enable ? "Enabling" : "Disabling");

    if (!mInjectorConnectionHandle) {
        auto vsyncSource = std::make_unique<InjectVSyncSource>();
        mVSyncInjector = vsyncSource.get();

        auto eventThread =
                std::make_unique<impl::EventThread>(std::move(vsyncSource),
                                                    impl::EventThread::InterceptVSyncsCallback());

        mInjectorConnectionHandle = createConnection(std::move(eventThread));
    }

    mInjectVSyncs = enable;
    return mInjectorConnectionHandle;
}

bool Scheduler::injectVSync(nsecs_t when) {
    if (!mInjectVSyncs || !mVSyncInjector) {
        return false;
    }

    mVSyncInjector->onInjectSyncEvent(when);
    return true;
}

void Scheduler::enableHardwareVsync() {
    std::lock_guard<std::mutex> lock(mHWVsyncLock);
    if (!mPrimaryHWVsyncEnabled && mHWVsyncAvailable) {
        mPrimaryDispSync->beginResync();
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void Scheduler::disableHardwareVsync(bool makeUnavailable) {
    std::lock_guard<std::mutex> lock(mHWVsyncLock);
    if (mPrimaryHWVsyncEnabled) {
        mEventControlThread->setVsyncEnabled(false);
        mPrimaryDispSync->endResync();
        mPrimaryHWVsyncEnabled = false;
    }
    if (makeUnavailable) {
        mHWVsyncAvailable = false;
    }
}

void Scheduler::resyncToHardwareVsync(bool makeAvailable, nsecs_t period) {
    {
        std::lock_guard<std::mutex> lock(mHWVsyncLock);
        if (makeAvailable) {
            mHWVsyncAvailable = makeAvailable;
        } else if (!mHWVsyncAvailable) {
            // Hardware vsync is not currently available, so abort the resync
            // attempt for now
            return;
        }
    }

    if (period <= 0) {
        return;
    }

    setVsyncPeriod(period);
}

void Scheduler::resync() {
    static constexpr nsecs_t kIgnoreDelay = ms2ns(750);

    const nsecs_t now = systemTime();
    const nsecs_t last = mLastResyncTime.exchange(now);

    if (now - last > kIgnoreDelay) {
        resyncToHardwareVsync(false, mRefreshRateConfigs.getCurrentRefreshRate().vsyncPeriod);
    }
}

void Scheduler::setVsyncPeriod(nsecs_t period) {
    std::lock_guard<std::mutex> lock(mHWVsyncLock);
    mPrimaryDispSync->setPeriod(period);

    if (!mPrimaryHWVsyncEnabled) {
        mPrimaryDispSync->beginResync();
        mEventControlThread->setVsyncEnabled(true);
        mPrimaryHWVsyncEnabled = true;
    }
}

void Scheduler::addResyncSample(nsecs_t timestamp, bool* periodFlushed) {
    bool needsHwVsync = false;
    *periodFlushed = false;
    { // Scope for the lock
        std::lock_guard<std::mutex> lock(mHWVsyncLock);
        if (mPrimaryHWVsyncEnabled) {
            needsHwVsync = mPrimaryDispSync->addResyncSample(timestamp, periodFlushed);
        }
    }

    if (needsHwVsync) {
        enableHardwareVsync();
    } else {
        disableHardwareVsync(false);
    }
}

void Scheduler::addPresentFence(const std::shared_ptr<FenceTime>& fenceTime) {
    if (mPrimaryDispSync->addPresentFence(fenceTime)) {
        enableHardwareVsync();
    } else {
        disableHardwareVsync(false);
    }
}

void Scheduler::setIgnorePresentFences(bool ignore) {
    mPrimaryDispSync->setIgnorePresentFences(ignore);
}

nsecs_t Scheduler::getDispSyncExpectedPresentTime() {
    return mPrimaryDispSync->expectedPresentTime();
}

void Scheduler::registerLayer(Layer* layer) {
    if (!mLayerHistory) return;

    const auto lowFps = mRefreshRateConfigs.getMinRefreshRate().fps;
    const auto highFps = layer->getWindowType() == InputWindowInfo::TYPE_WALLPAPER
            ? lowFps
            : mRefreshRateConfigs.getMaxRefreshRate().fps;

    mLayerHistory->registerLayer(layer, lowFps, highFps);
}

void Scheduler::recordLayerHistory(Layer* layer, nsecs_t presentTime) {
    if (mLayerHistory) {
        mLayerHistory->record(layer, presentTime, systemTime());
    }
}

void Scheduler::chooseRefreshRateForContent() {
    if (!mLayerHistory) return;

    auto [refreshRate] = mLayerHistory->summarize(systemTime());
    const uint32_t refreshRateRound = std::round(refreshRate);
    HwcConfigIndexType newConfigId;
    {
        std::lock_guard<std::mutex> lock(mFeatureStateLock);
        if (mFeatures.contentRefreshRate == refreshRateRound) {
            return;
        }
        mFeatures.contentRefreshRate = refreshRateRound;
        ATRACE_INT("ContentFPS", refreshRateRound);

        mFeatures.contentDetection =
                refreshRateRound > 0 ? ContentDetectionState::On : ContentDetectionState::Off;
        newConfigId = calculateRefreshRateType();
        if (mFeatures.configId == newConfigId) {
            return;
        }
        mFeatures.configId = newConfigId;
    };
    auto newRefreshRate = mRefreshRateConfigs.getRefreshRateFromConfigId(newConfigId);
    mSchedulerCallback.changeRefreshRate(newRefreshRate, ConfigEvent::Changed);
}

void Scheduler::resetIdleTimer() {
    if (mIdleTimer) {
        mIdleTimer->reset();
    }
}

void Scheduler::notifyTouchEvent() {
    if (mTouchTimer) {
        mTouchTimer->reset();
    }

    if (mSupportKernelTimer && mIdleTimer) {
        mIdleTimer->reset();
    }

    // Touch event will boost the refresh rate to performance.
    // Clear Layer History to get fresh FPS detection
    if (mLayerHistory) {
        mLayerHistory->clear();
    }
}

void Scheduler::setDisplayPowerState(bool normal) {
    {
        std::lock_guard<std::mutex> lock(mFeatureStateLock);
        mFeatures.isDisplayPowerStateNormal = normal;
    }

    if (mDisplayPowerTimer) {
        mDisplayPowerTimer->reset();
    }

    // Display Power event will boost the refresh rate to performance.
    // Clear Layer History to get fresh FPS detection
    if (mLayerHistory) {
        mLayerHistory->clear();
    }
}

void Scheduler::kernelIdleTimerCallback(TimerState state) {
    ATRACE_INT("ExpiredKernelIdleTimer", static_cast<int>(state));

    // TODO(145561154): cleanup the kernel idle timer implementation and the refresh rate
    // magic number
    const auto refreshRate = mRefreshRateConfigs.getCurrentRefreshRate();
    constexpr float FPS_THRESHOLD_FOR_KERNEL_TIMER = 65.0f;
    if (state == TimerState::Reset && refreshRate.fps > FPS_THRESHOLD_FOR_KERNEL_TIMER) {
        // If we're not in performance mode then the kernel timer shouldn't do
        // anything, as the refresh rate during DPU power collapse will be the
        // same.
        resyncToHardwareVsync(true /* makeAvailable */, refreshRate.vsyncPeriod);
    } else if (state == TimerState::Expired && refreshRate.fps <= FPS_THRESHOLD_FOR_KERNEL_TIMER) {
        // Disable HW VSYNC if the timer expired, as we don't need it enabled if
        // we're not pushing frames, and if we're in PERFORMANCE mode then we'll
        // need to update the DispSync model anyway.
        disableHardwareVsync(false /* makeUnavailable */);
    }
}

void Scheduler::idleTimerCallback(TimerState state) {
    handleTimerStateChanged(&mFeatures.idleTimer, state, false /* eventOnContentDetection */);
    ATRACE_INT("ExpiredIdleTimer", static_cast<int>(state));
}

void Scheduler::touchTimerCallback(TimerState state) {
    const TouchState touch = state == TimerState::Reset ? TouchState::Active : TouchState::Inactive;
    handleTimerStateChanged(&mFeatures.touch, touch, true /* eventOnContentDetection */);
    ATRACE_INT("TouchState", static_cast<int>(touch));
}

void Scheduler::displayPowerTimerCallback(TimerState state) {
    handleTimerStateChanged(&mFeatures.displayPowerTimer, state,
                            true /* eventOnContentDetection */);
    ATRACE_INT("ExpiredDisplayPowerTimer", static_cast<int>(state));
}

void Scheduler::dump(std::string& result) const {
    using base::StringAppendF;
    const char* const states[] = {"off", "on"};

    const bool supported = mRefreshRateConfigs.refreshRateSwitchingSupported();
    StringAppendF(&result, "+  Refresh rate switching: %s\n", states[supported]);
    StringAppendF(&result, "+  Content detection: %s\n", states[mLayerHistory.has_value()]);

    StringAppendF(&result, "+  Idle timer: %s\n",
                  mIdleTimer ? mIdleTimer->dump().c_str() : states[0]);
    StringAppendF(&result, "+  Touch timer: %s\n\n",
                  mTouchTimer ? mTouchTimer->dump().c_str() : states[0]);
}

template <class T>
void Scheduler::handleTimerStateChanged(T* currentState, T newState, bool eventOnContentDetection) {
    ConfigEvent event = ConfigEvent::None;
    HwcConfigIndexType newConfigId;
    {
        std::lock_guard<std::mutex> lock(mFeatureStateLock);
        if (*currentState == newState) {
            return;
        }
        *currentState = newState;
        newConfigId = calculateRefreshRateType();
        if (mFeatures.configId == newConfigId) {
            return;
        }
        mFeatures.configId = newConfigId;
        if (eventOnContentDetection && mFeatures.contentDetection == ContentDetectionState::On) {
            event = ConfigEvent::Changed;
        }
    }
    const RefreshRate& newRefreshRate = mRefreshRateConfigs.getRefreshRateFromConfigId(newConfigId);
    mSchedulerCallback.changeRefreshRate(newRefreshRate, event);
}

HwcConfigIndexType Scheduler::calculateRefreshRateType() {
    if (!mRefreshRateConfigs.refreshRateSwitchingSupported()) {
        return mRefreshRateConfigs.getCurrentRefreshRate().configId;
    }

    // If Display Power is not in normal operation we want to be in performance mode.
    // When coming back to normal mode, a grace period is given with DisplayPowerTimer
    if (!mFeatures.isDisplayPowerStateNormal || mFeatures.displayPowerTimer == TimerState::Reset) {
        return mRefreshRateConfigs.getMaxRefreshRateByPolicy().configId;
    }

    // As long as touch is active we want to be in performance mode
    if (mFeatures.touch == TouchState::Active) {
        return mRefreshRateConfigs.getMaxRefreshRateByPolicy().configId;
    }

    // If timer has expired as it means there is no new content on the screen
    if (mFeatures.idleTimer == TimerState::Expired) {
        return mRefreshRateConfigs.getMinRefreshRateByPolicy().configId;
    }

    // If content detection is off we choose performance as we don't know the content fps
    if (mFeatures.contentDetection == ContentDetectionState::Off) {
        return mRefreshRateConfigs.getMaxRefreshRateByPolicy().configId;
    }

    // Content detection is on, find the appropriate refresh rate with minimal error
    return mRefreshRateConfigs
            .getRefreshRateForContent(static_cast<float>(mFeatures.contentRefreshRate))
            .configId;
}

std::optional<HwcConfigIndexType> Scheduler::getPreferredConfigId() {
    std::lock_guard<std::mutex> lock(mFeatureStateLock);
    return mFeatures.configId;
}

void Scheduler::onNewVsyncPeriodChangeTimeline(const HWC2::VsyncPeriodChangeTimeline& timeline) {
    if (timeline.refreshRequired) {
        mSchedulerCallback.repaintEverythingForHWC();
    }

    std::lock_guard<std::mutex> lock(mVsyncTimelineLock);
    mLastVsyncPeriodChangeTimeline = std::make_optional(timeline);

    const auto maxAppliedTime = systemTime() + MAX_VSYNC_APPLIED_TIME.count();
    if (timeline.newVsyncAppliedTimeNanos > maxAppliedTime) {
        mLastVsyncPeriodChangeTimeline->newVsyncAppliedTimeNanos = maxAppliedTime;
    }
}

void Scheduler::onDisplayRefreshed(nsecs_t timestamp) {
    bool callRepaint = false;
    {
        std::lock_guard<std::mutex> lock(mVsyncTimelineLock);
        if (mLastVsyncPeriodChangeTimeline && mLastVsyncPeriodChangeTimeline->refreshRequired) {
            if (mLastVsyncPeriodChangeTimeline->refreshTimeNanos < timestamp) {
                mLastVsyncPeriodChangeTimeline->refreshRequired = false;
            } else {
                // We need to send another refresh as refreshTimeNanos is still in the future
                callRepaint = true;
            }
        }
    }

    if (callRepaint) {
        mSchedulerCallback.repaintEverythingForHWC();
    }
}

} // namespace android
