/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <thread>
#include "LayerTransactionTest.h"
namespace android {

using android::hardware::graphics::common::V1_1::BufferUsage;

::testing::Environment* const binderEnv =
        ::testing::AddGlobalTestEnvironment(new BinderEnvironment());

/**
 * Test class for setting display configs and passing around refresh rate ranges.
 */
class RefreshRateRangeTest : public ::testing::Test {
protected:
    void SetUp() override { mDisplayToken = SurfaceComposerClient::getInternalDisplayToken(); }

    sp<IBinder> mDisplayToken;
    int32_t defaultConfigId;
    float minRefreshRate;
    float maxRefreshRate;
};

TEST_F(RefreshRateRangeTest, simpleSetAndGet) {
    status_t res = SurfaceComposerClient::setDesiredDisplayConfigSpecs(mDisplayToken, 1, 45, 75);
    EXPECT_EQ(res, NO_ERROR);

    res = SurfaceComposerClient::getDesiredDisplayConfigSpecs(mDisplayToken, &defaultConfigId,
                                                              &minRefreshRate, &maxRefreshRate);
    EXPECT_EQ(res, NO_ERROR);
    EXPECT_EQ(defaultConfigId, 1);
    EXPECT_EQ(minRefreshRate, 45);
    EXPECT_EQ(maxRefreshRate, 75);
}

TEST_F(RefreshRateRangeTest, complexSetAndGet) {
    status_t res = SurfaceComposerClient::setDesiredDisplayConfigSpecs(mDisplayToken, 1, 45, 75);
    EXPECT_EQ(res, NO_ERROR);

    res = SurfaceComposerClient::getDesiredDisplayConfigSpecs(mDisplayToken, &defaultConfigId,
                                                              &minRefreshRate, &maxRefreshRate);
    EXPECT_EQ(res, NO_ERROR);
    EXPECT_EQ(defaultConfigId, 1);
    EXPECT_EQ(minRefreshRate, 45);
    EXPECT_EQ(maxRefreshRate, 75);

    // Second call overrides the first one.
    res = SurfaceComposerClient::setDesiredDisplayConfigSpecs(mDisplayToken, 10, 145, 875);
    EXPECT_EQ(res, NO_ERROR);
    res = SurfaceComposerClient::getDesiredDisplayConfigSpecs(mDisplayToken, &defaultConfigId,
                                                              &minRefreshRate, &maxRefreshRate);
    EXPECT_EQ(res, NO_ERROR);
    EXPECT_EQ(defaultConfigId, 10);
    EXPECT_EQ(minRefreshRate, 145);
    EXPECT_EQ(maxRefreshRate, 875);
}
} // namespace android
