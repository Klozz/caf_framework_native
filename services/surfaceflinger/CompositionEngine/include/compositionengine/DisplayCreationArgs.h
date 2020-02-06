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

#include <cstdint>
#include <optional>

#include "DisplayHardware/DisplayIdentification.h"
#include "DisplayHardware/PowerAdvisor.h"

namespace android::compositionengine {

class CompositionEngine;

/**
 * A parameter object for creating Display instances
 */
struct DisplayCreationArgs {
    // True if this display is a virtual display
    bool isVirtual = false;

    // Identifies the display to the HWC, if composition is supported by it
    std::optional<DisplayId> displayId;

    // Optional pointer to the power advisor interface, if one is needed for
    // this display.
    Hwc2::PowerAdvisor* powerAdvisor = nullptr;
};

/**
 * A helper for setting up a DisplayCreationArgs value in-line.
 * Prefer this builder over raw structure initialization.
 *
 * Instead of:
 *
 *   DisplayCreationArgs{false, false, displayId}
 *
 * Prefer:
 *
 *  DisplayCreationArgsBuilder().setIsVirtual(false)
 *      .setDisplayId(displayId).build();
 */
class DisplayCreationArgsBuilder {
public:
    DisplayCreationArgs build() { return std::move(mArgs); }

    DisplayCreationArgsBuilder& setIsVirtual(bool isVirtual) {
        mArgs.isVirtual = isVirtual;
        return *this;
    }
    DisplayCreationArgsBuilder& setDisplayId(std::optional<DisplayId> displayId) {
        mArgs.displayId = displayId;
        return *this;
    }
    DisplayCreationArgsBuilder& setPowerAdvisor(Hwc2::PowerAdvisor* powerAdvisor) {
        mArgs.powerAdvisor = powerAdvisor;
        return *this;
    }

private:
    DisplayCreationArgs mArgs;
};

} // namespace android::compositionengine
