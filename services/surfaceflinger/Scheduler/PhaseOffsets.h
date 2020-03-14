/*
 * Copyright 2019 The Android Open Source Project
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

#pragma once

#include <unordered_map>

#include "RefreshRateConfigs.h"
#include "VSyncModulator.h"

namespace android::scheduler {

/*
 * This class encapsulates offsets for different refresh rates. Depending
 * on what refresh rate we are using, and wheter we are composing in GL,
 * different offsets will help us with latency. This class keeps track of
 * which mode the device is on, and returns approprate offsets when needed.
 */
class PhaseOffsets {
public:
    using Offsets = VSyncModulator::OffsetsConfig;

    virtual ~PhaseOffsets();

    nsecs_t getCurrentAppOffset() const { return getCurrentOffsets().late.app; }
    nsecs_t getCurrentSfOffset() const { return getCurrentOffsets().late.sf; }
    nsecs_t getOffsetThresholdForNextVsync() const {
        return getCurrentOffsets().thresholdForNextVsync;
    }

    virtual Offsets getCurrentOffsets() const = 0;
    virtual Offsets getOffsetsForRefreshRate(float fps) const = 0;

    virtual void setRefreshRateFps(float fps) = 0;

    virtual void dump(std::string& result) const = 0;
};

namespace impl {

class PhaseOffsets : public scheduler::PhaseOffsets {
public:
    PhaseOffsets();

    // Returns early, early GL, and late offsets for Apps and SF for a given refresh rate.
    Offsets getOffsetsForRefreshRate(float fps) const override;

    // Returns early, early GL, and late offsets for Apps and SF.
    Offsets getCurrentOffsets() const override { return getOffsetsForRefreshRate(mRefreshRateFps); }

    // This function should be called when the device is switching between different
    // refresh rates, to properly update the offsets.
    void setRefreshRateFps(float fps) override { mRefreshRateFps = fps; }

    // Returns current offsets in human friendly format.
    void dump(std::string& result) const override;

private:
    static Offsets getDefaultOffsets(nsecs_t thresholdForNextVsync);
    static Offsets getHighFpsOffsets(nsecs_t thresholdForNextVsync);

    std::atomic<float> mRefreshRateFps = 0;

    Offsets mDefaultOffsets;
    Offsets mHighFpsOffsets;
};

} // namespace impl
} // namespace android::scheduler
