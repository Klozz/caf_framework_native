/*
 * Copyright (C) 2007 The Android Open Source Project
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

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

// #define LOG_NDEBUG 0
#undef LOG_TAG
#define LOG_TAG "DisplayDevice"

#include <android-base/stringprintf.h>
#include <compositionengine/CompositionEngine.h>
#include <compositionengine/Display.h>
#include <compositionengine/DisplayColorProfile.h>
#include <compositionengine/DisplayColorProfileCreationArgs.h>
#include <compositionengine/DisplayCreationArgs.h>
#include <compositionengine/DisplaySurface.h>
#include <compositionengine/RenderSurface.h>
#include <compositionengine/RenderSurfaceCreationArgs.h>
#include <compositionengine/impl/OutputCompositionState.h>
#include <configstore/Utils.h>
#include <log/log.h>
#include <system/window.h>
#include <ui/GraphicTypes.h>

#include "DisplayDevice.h"
#include "Layer.h"
#include "SurfaceFlinger.h"

namespace android {

using android::base::StringAppendF;

ui::Transform::RotationFlags DisplayDevice::sPrimaryDisplayRotationFlags = ui::Transform::ROT_0;

DisplayDeviceCreationArgs::DisplayDeviceCreationArgs(
        const sp<SurfaceFlinger>& flinger, const wp<IBinder>& displayToken,
        std::shared_ptr<compositionengine::Display> compositionDisplay)
      : flinger(flinger), displayToken(displayToken), compositionDisplay(compositionDisplay) {}

DisplayDevice::DisplayDevice(DisplayDeviceCreationArgs& args)
      : mFlinger(args.flinger),
        mDisplayToken(args.displayToken),
        mSequenceId(args.sequenceId),
        mConnectionType(args.connectionType),
        mCompositionDisplay{args.compositionDisplay},
        mPhysicalOrientation(args.physicalOrientation),
        mIsPrimary(args.isPrimary) {
    mCompositionDisplay->editState().isSecure = args.isSecure;
    mCompositionDisplay->createRenderSurface(
            compositionengine::RenderSurfaceCreationArgs{ANativeWindow_getWidth(
                                                                 args.nativeWindow.get()),
                                                         ANativeWindow_getHeight(
                                                                 args.nativeWindow.get()),
                                                         args.nativeWindow, args.displaySurface});

    if (!mFlinger->mDisableClientCompositionCache &&
        SurfaceFlinger::maxFrameBufferAcquiredBuffers > 0) {
        mCompositionDisplay->createClientCompositionCache(
                static_cast<uint32_t>(SurfaceFlinger::maxFrameBufferAcquiredBuffers));
    }

    mCompositionDisplay->createDisplayColorProfile(
            compositionengine::DisplayColorProfileCreationArgs{args.hasWideColorGamut,
                                                               std::move(args.hdrCapabilities),
                                                               args.supportedPerFrameMetadata,
                                                               args.hwcColorModes});

    if (!mCompositionDisplay->isValid()) {
        ALOGE("Composition Display did not validate!");
    }

    mCompositionDisplay->getRenderSurface()->initialize();

    setPowerMode(args.initialPowerMode);

    // initialize the display orientation transform.
    setProjection(ui::ROTATION_0, Rect::INVALID_RECT, Rect::INVALID_RECT);
}

DisplayDevice::~DisplayDevice() = default;

void DisplayDevice::disconnect() {
    mCompositionDisplay->disconnect();
}

int DisplayDevice::getWidth() const {
    return mCompositionDisplay->getState().bounds.getWidth();
}

int DisplayDevice::getHeight() const {
    return mCompositionDisplay->getState().bounds.getHeight();
}

void DisplayDevice::setDisplayName(const std::string& displayName) {
    if (!displayName.empty()) {
        // never override the name with an empty name
        mDisplayName = displayName;
        mCompositionDisplay->setName(displayName);
    }
}

uint32_t DisplayDevice::getPageFlipCount() const {
    return mCompositionDisplay->getRenderSurface()->getPageFlipCount();
}

// ----------------------------------------------------------------------------
void DisplayDevice::setPowerMode(int mode) {
    mPowerMode = mode;
    getCompositionDisplay()->setCompositionEnabled(mPowerMode != HWC_POWER_MODE_OFF);
}

int DisplayDevice::getPowerMode()  const {
    return mPowerMode;
}

bool DisplayDevice::isPoweredOn() const {
    return mPowerMode != HWC_POWER_MODE_OFF;
}

void DisplayDevice::setActiveConfig(HwcConfigIndexType mode) {
    mActiveConfig = mode;
}

HwcConfigIndexType DisplayDevice::getActiveConfig() const {
    return mActiveConfig;
}

ui::Dataspace DisplayDevice::getCompositionDataSpace() const {
    return mCompositionDisplay->getState().dataspace;
}

void DisplayDevice::setLayerStack(ui::LayerStack stack) {
    mCompositionDisplay->setLayerStackFilter(stack, isPrimary());
}

void DisplayDevice::setDisplaySize(int width, int height) {
    mCompositionDisplay->setBounds(ui::Size(width, height));
}

void DisplayDevice::setProjection(ui::Rotation orientation, Rect viewport, Rect frame) {
    mOrientation = orientation;

    const Rect& displayBounds = getCompositionDisplay()->getState().bounds;
    const int w = displayBounds.width();
    const int h = displayBounds.height();

    ui::Transform R;
    if (const auto flags = ui::Transform::toRotationFlags(orientation);
        flags != ui::Transform::ROT_INVALID) {
        R.set(flags, w, h);
    }

    if (!frame.isValid()) {
        // the destination frame can be invalid if it has never been set,
        // in that case we assume the whole display frame.
        frame = Rect(w, h);
    }

    if (viewport.isEmpty()) {
        // viewport can be invalid if it has never been set, in that case
        // we assume the whole display size.
        // it's also invalid to have an empty viewport, so we handle that
        // case in the same way.
        viewport = Rect(w, h);
        if (R.getOrientation() & ui::Transform::ROT_90) {
            // viewport is always specified in the logical orientation
            // of the display (ie: post-rotation).
            std::swap(viewport.right, viewport.bottom);
        }
    }

    ui::Transform TL, TP, S;
    float src_width  = viewport.width();
    float src_height = viewport.height();
    float dst_width  = frame.width();
    float dst_height = frame.height();
    if (src_width != dst_width || src_height != dst_height) {
        float sx = dst_width  / src_width;
        float sy = dst_height / src_height;
        S.set(sx, 0, 0, sy);
    }

    float src_x = viewport.left;
    float src_y = viewport.top;
    float dst_x = frame.left;
    float dst_y = frame.top;
    TL.set(-src_x, -src_y);
    TP.set(dst_x, dst_y);

    // need to take care of primary display rotation for globalTransform
    // for case if the panel is not installed aligned with device orientation
    if (isPrimary()) {
        if (const auto flags = ui::Transform::toRotationFlags(orientation + mPhysicalOrientation);
            flags != ui::Transform::ROT_INVALID) {
            R.set(flags, w, h);
        }
    }

    // The viewport and frame are both in the logical orientation.
    // Apply the logical translation, scale to physical size, apply the
    // physical translation and finally rotate to the physical orientation.
    ui::Transform globalTransform = R * TP * S * TL;

    const uint8_t type = globalTransform.getType();
    const bool needsFiltering =
            (!globalTransform.preserveRects() || (type >= ui::Transform::SCALE));

    const Rect& sourceClip = viewport;
    Rect destinationClip = globalTransform.transform(viewport);
    if (destinationClip.isEmpty()) {
        destinationClip = displayBounds;
    }

    uint32_t transformOrientation;

    if (isPrimary()) {
        sPrimaryDisplayRotationFlags = ui::Transform::toRotationFlags(orientation);
        transformOrientation = ui::Transform::toRotationFlags(orientation + mPhysicalOrientation);
    } else {
        transformOrientation = ui::Transform::toRotationFlags(orientation);
    }

    getCompositionDisplay()->setProjection(globalTransform, transformOrientation, frame, viewport,
                                           sourceClip, destinationClip, needsFiltering);
}

ui::Transform::RotationFlags DisplayDevice::getPrimaryDisplayRotationFlags() {
    return sPrimaryDisplayRotationFlags;
}

std::string DisplayDevice::getDebugName() const {
    std::string displayId;
    if (const auto id = getId()) {
        displayId = to_string(*id) + ", ";
    }

    const char* type = "virtual";
    if (mConnectionType) {
        type = *mConnectionType == DisplayConnectionType::Internal ? "internal" : "external";
    }

    return base::StringPrintf("DisplayDevice{%s%s%s, \"%s\"}", displayId.c_str(), type,
                              isPrimary() ? ", primary" : "", mDisplayName.c_str());
}

void DisplayDevice::dump(std::string& result) const {
    StringAppendF(&result, "+ %s\n", getDebugName().c_str());

    result.append("   ");
    StringAppendF(&result, "powerMode=%d, ", mPowerMode);
    StringAppendF(&result, "activeConfig=%d, ", mActiveConfig.value());
    getCompositionDisplay()->dump(result);
}

bool DisplayDevice::hasRenderIntent(ui::RenderIntent intent) const {
    return mCompositionDisplay->getDisplayColorProfile()->hasRenderIntent(intent);
}

// ----------------------------------------------------------------------------

const std::optional<DisplayId>& DisplayDevice::getId() const {
    return mCompositionDisplay->getId();
}

bool DisplayDevice::isSecure() const {
    return mCompositionDisplay->isSecure();
}

const Rect& DisplayDevice::getBounds() const {
    return mCompositionDisplay->getState().bounds;
}

const Region& DisplayDevice::getUndefinedRegion() const {
    return mCompositionDisplay->getState().undefinedRegion;
}

bool DisplayDevice::needsFiltering() const {
    return mCompositionDisplay->getState().needsFiltering;
}

ui::LayerStack DisplayDevice::getLayerStack() const {
    return mCompositionDisplay->getState().layerStackId;
}

const ui::Transform& DisplayDevice::getTransform() const {
    return mCompositionDisplay->getState().transform;
}

const Rect& DisplayDevice::getViewport() const {
    return mCompositionDisplay->getState().viewport;
}

const Rect& DisplayDevice::getFrame() const {
    return mCompositionDisplay->getState().frame;
}

const Rect& DisplayDevice::getSourceClip() const {
    return mCompositionDisplay->getState().sourceClip;
}

bool DisplayDevice::hasWideColorGamut() const {
    return mCompositionDisplay->getDisplayColorProfile()->hasWideColorGamut();
}

bool DisplayDevice::hasHDR10PlusSupport() const {
    return mCompositionDisplay->getDisplayColorProfile()->hasHDR10PlusSupport();
}

bool DisplayDevice::hasHDR10Support() const {
    return mCompositionDisplay->getDisplayColorProfile()->hasHDR10Support();
}

bool DisplayDevice::hasHLGSupport() const {
    return mCompositionDisplay->getDisplayColorProfile()->hasHLGSupport();
}

bool DisplayDevice::hasDolbyVisionSupport() const {
    return mCompositionDisplay->getDisplayColorProfile()->hasDolbyVisionSupport();
}

int DisplayDevice::getSupportedPerFrameMetadata() const {
    return mCompositionDisplay->getDisplayColorProfile()->getSupportedPerFrameMetadata();
}

const HdrCapabilities& DisplayDevice::getHdrCapabilities() const {
    return mCompositionDisplay->getDisplayColorProfile()->getHdrCapabilities();
}

std::atomic<int32_t> DisplayDeviceState::sNextSequenceId(1);

}  // namespace android

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic pop // ignored "-Wconversion"
