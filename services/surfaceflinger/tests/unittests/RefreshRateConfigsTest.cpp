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

#undef LOG_TAG
#define LOG_TAG "SchedulerUnittests"

#include <gmock/gmock.h>
#include <log/log.h>
#include <thread>

#include "DisplayHardware/HWC2.h"
#include "Scheduler/RefreshRateConfigs.h"

using namespace std::chrono_literals;
using testing::_;

namespace android {
namespace scheduler {

using RefreshRate = RefreshRateConfigs::RefreshRate;
using LayerVoteType = RefreshRateConfigs::LayerVoteType;
using LayerRequirement = RefreshRateConfigs::LayerRequirement;

class RefreshRateConfigsTest : public testing::Test {
protected:
    static inline const HwcConfigIndexType HWC_CONFIG_ID_60 = HwcConfigIndexType(0);
    static inline const HwcConfigIndexType HWC_CONFIG_ID_72 = HwcConfigIndexType(1);
    static inline const HwcConfigIndexType HWC_CONFIG_ID_90 = HwcConfigIndexType(2);
    static inline const HwcConfigIndexType HWC_CONFIG_ID_120 = HwcConfigIndexType(3);
    static inline const HwcConfigIndexType HWC_CONFIG_ID_30 = HwcConfigIndexType(4);
    static inline const HwcConfigGroupType HWC_GROUP_ID_0 = HwcConfigGroupType(0);
    static inline const HwcConfigGroupType HWC_GROUP_ID_1 = HwcConfigGroupType(1);
    static constexpr auto VSYNC_30 = static_cast<int64_t>(1e9f / 30);
    static constexpr auto VSYNC_60 = static_cast<int64_t>(1e9f / 60);
    static constexpr auto VSYNC_72 = static_cast<int64_t>(1e9f / 72);
    static constexpr auto VSYNC_90 = static_cast<int64_t>(1e9f / 90);
    static constexpr auto VSYNC_120 = static_cast<int64_t>(1e9f / 120);
    static constexpr int64_t VSYNC_60_POINT_4 = 16666665;

    RefreshRateConfigsTest();
    ~RefreshRateConfigsTest();
};

RefreshRateConfigsTest::RefreshRateConfigsTest() {
    const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
    ALOGD("**** Setting up for %s.%s\n", test_info->test_case_name(), test_info->name());
}

RefreshRateConfigsTest::~RefreshRateConfigsTest() {
    const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
    ALOGD("**** Tearing down after %s.%s\n", test_info->test_case_name(), test_info->name());
}

namespace {
/* ------------------------------------------------------------------------
 * Test cases
 */
TEST_F(RefreshRateConfigsTest, oneDeviceConfig_SwitchingSupported) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);
}

TEST_F(RefreshRateConfigsTest, invalidPolicy) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);
    ASSERT_LT(refreshRateConfigs->setPolicy(HwcConfigIndexType(10), 60, 60, nullptr), 0);
    ASSERT_LT(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 20, 40, nullptr), 0);
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_storesFullRefreshRateMap) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    const auto& minRate = refreshRateConfigs->getMinRefreshRate();
    const auto& performanceRate = refreshRateConfigs->getMaxRefreshRate();

    RefreshRate expectedDefaultConfig = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    ASSERT_EQ(expectedDefaultConfig, minRate);
    RefreshRate expectedPerformanceConfig = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps",
                                             90};
    ASSERT_EQ(expectedPerformanceConfig, performanceRate);

    const auto& minRateByPolicy = refreshRateConfigs->getMinRefreshRateByPolicy();
    const auto& performanceRateByPolicy = refreshRateConfigs->getMaxRefreshRateByPolicy();
    ASSERT_EQ(minRateByPolicy, minRate);
    ASSERT_EQ(performanceRateByPolicy, performanceRate);
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_storesFullRefreshRateMap_differentGroups) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_1, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    const auto& minRate = refreshRateConfigs->getMinRefreshRateByPolicy();
    const auto& performanceRate = refreshRateConfigs->getMaxRefreshRate();
    const auto& minRate60 = refreshRateConfigs->getMinRefreshRateByPolicy();
    const auto& performanceRate60 = refreshRateConfigs->getMaxRefreshRateByPolicy();

    RefreshRate expectedDefaultConfig = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    ASSERT_EQ(expectedDefaultConfig, minRate);
    ASSERT_EQ(expectedDefaultConfig, minRate60);
    ASSERT_EQ(expectedDefaultConfig, performanceRate60);

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_90, 60, 90, nullptr), 0);
    refreshRateConfigs->setCurrentConfigId(HWC_CONFIG_ID_90);

    const auto& minRate90 = refreshRateConfigs->getMinRefreshRateByPolicy();
    const auto& performanceRate90 = refreshRateConfigs->getMaxRefreshRateByPolicy();

    RefreshRate expectedPerformanceConfig = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_1, "90fps",
                                             90};
    ASSERT_EQ(expectedPerformanceConfig, performanceRate);
    ASSERT_EQ(expectedPerformanceConfig, minRate90);
    ASSERT_EQ(expectedPerformanceConfig, performanceRate90);
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_policyChange) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    auto& minRate = refreshRateConfigs->getMinRefreshRateByPolicy();
    auto& performanceRate = refreshRateConfigs->getMaxRefreshRateByPolicy();

    RefreshRate expectedDefaultConfig = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    ASSERT_EQ(expectedDefaultConfig, minRate);
    RefreshRate expectedPerformanceConfig = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps",
                                             90};
    ASSERT_EQ(expectedPerformanceConfig, performanceRate);

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 60, 60, nullptr), 0);

    auto& minRate60 = refreshRateConfigs->getMinRefreshRateByPolicy();
    auto& performanceRate60 = refreshRateConfigs->getMaxRefreshRateByPolicy();
    ASSERT_EQ(expectedDefaultConfig, minRate60);
    ASSERT_EQ(expectedDefaultConfig, performanceRate60);
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_getCurrentRefreshRate) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);
    {
        auto& current = refreshRateConfigs->getCurrentRefreshRate();
        EXPECT_EQ(current.configId, HWC_CONFIG_ID_60);
    }

    refreshRateConfigs->setCurrentConfigId(HWC_CONFIG_ID_90);
    {
        auto& current = refreshRateConfigs->getCurrentRefreshRate();
        EXPECT_EQ(current.configId, HWC_CONFIG_ID_90);
    }

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_90, 90, 90, nullptr), 0);
    {
        auto& current = refreshRateConfigs->getCurrentRefreshRate();
        EXPECT_EQ(current.configId, HWC_CONFIG_ID_90);
    }
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_getRefreshRateForContent) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    const auto makeLayerRequirements = [](float refreshRate) -> std::vector<LayerRequirement> {
        return {{"testLayer", LayerVoteType::Heuristic, refreshRate, 1.0f}};
    };

    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(90.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(60.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(45.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(30.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(24.0f)));

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 60, 60, nullptr), 0);
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(90.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(60.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(45.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(30.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(24.0f)));

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_90, 90, 90, nullptr), 0);
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(90.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(60.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(45.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(30.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(24.0f)));
    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 0, 120, nullptr), 0);
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(90.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(60.0f)));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(45.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(30.0f)));
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContent(makeLayerRequirements(24.0f)));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_noLayers) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_72, HWC_GROUP_ID_0, VSYNC_72},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs = std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/
                                                                   HWC_CONFIG_ID_72);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected72Config = {HWC_CONFIG_ID_72, VSYNC_72, HWC_GROUP_ID_0, "72fps", 72};

    // If there are not layers, there is not content detection, so return the current
    // refresh rate.
    auto layers = std::vector<LayerRequirement>{};
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/
                                                             false));

    // Current refresh rate can always be changed.
    refreshRateConfigs->setCurrentConfigId(HWC_CONFIG_ID_60);
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/
                                                             false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_60_90) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::Min;
    lr.name = "Min";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    lr.name = "Max";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    lr.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    lr.name = "60Hz Heuristic";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    lr.name = "45Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    lr.name = "30Hz Heuristic";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    lr.name = "24Hz Heuristic";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.name = "";
    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 60, 60, nullptr), 0);

    lr.vote = LayerVoteType::Min;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_90, 90, 90, nullptr), 0);

    lr.vote = LayerVoteType::Min;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    ASSERT_GE(refreshRateConfigs->setPolicy(HWC_CONFIG_ID_60, 0, 120, nullptr), 0);
    lr.vote = LayerVoteType::Min;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_60_72_90) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_72, HWC_GROUP_ID_0, VSYNC_72},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected72Config = {HWC_CONFIG_ID_72, VSYNC_72, HWC_GROUP_ID_0, "72fps", 70};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::Min;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_30_60_72_90_120) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_30, HWC_GROUP_ID_0, VSYNC_30},
             {HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_72, HWC_GROUP_ID_0, VSYNC_72},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90},
             {HWC_CONFIG_ID_120, HWC_GROUP_ID_0, VSYNC_120}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected72Config = {HWC_CONFIG_ID_72, VSYNC_72, HWC_GROUP_ID_0, "72fps", 70};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};
    RefreshRate expected120Config = {HWC_CONFIG_ID_120, VSYNC_120, HWC_GROUP_ID_0, "120fps", 120};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected120Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 48.0f;
    lr2.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 48.0f;
    lr2.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_30_60_90_120_DifferentTypes) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_30, HWC_GROUP_ID_0, VSYNC_30},
             {HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_72, HWC_GROUP_ID_0, VSYNC_72},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90},
             {HWC_CONFIG_ID_120, HWC_GROUP_ID_0, VSYNC_120}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected72Config = {HWC_CONFIG_ID_72, VSYNC_72, HWC_GROUP_ID_0, "72fps", 72};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};
    RefreshRate expected120Config = {HWC_CONFIG_ID_120, VSYNC_120, HWC_GROUP_ID_0, "120fps", 120};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitDefault;
    lr1.name = "24Hz ExplicitDefault";
    lr2.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.name = "60Hz Heuristic";
    EXPECT_EQ(expected120Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.name = "24Hz ExplicitExactOrMultiple";
    lr2.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.name = "60Hz Heuristic";
    EXPECT_EQ(expected120Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.name = "24Hz ExplicitExactOrMultiple";
    lr2.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::ExplicitDefault;
    lr2.name = "60Hz ExplicitDefault";
    EXPECT_EQ(expected120Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.name = "24Hz ExplicitExactOrMultiple";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.name = "24Hz ExplicitExactOrMultiple";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitDefault;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitDefault;
    lr1.name = "24Hz ExplicitDefault";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::Heuristic;
    lr1.name = "24Hz Heuristic";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitDefault;
    lr2.name = "90Hz ExplicitDefault";
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.name = "24Hz ExplicitExactOrMultiple";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitDefault;
    lr2.name = "90Hz ExplicitDefault";
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.desiredRefreshRate = 24.0f;
    lr1.vote = LayerVoteType::ExplicitDefault;
    lr1.name = "24Hz ExplicitDefault";
    lr2.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.name = "90Hz ExplicitExactOrMultiple";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_30_60) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_30, HWC_GROUP_ID_0, VSYNC_30}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::Min;
    EXPECT_EQ(expected30Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 30.0f;
    EXPECT_EQ(expected30Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_30_60_72_90) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_30, HWC_GROUP_ID_0, VSYNC_30},
             {HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_72, HWC_GROUP_ID_0, VSYNC_72},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected72Config = {HWC_CONFIG_ID_72, VSYNC_72, HWC_GROUP_ID_0, "72fps", 70};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::Min;
    lr.name = "Min";
    EXPECT_EQ(expected30Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.vote = LayerVoteType::Max;
    lr.name = "Max";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 90.0f;
    lr.vote = LayerVoteType::Heuristic;
    lr.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr.desiredRefreshRate = 60.0f;
    lr.name = "60Hz Heuristic";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ true));

    lr.desiredRefreshRate = 45.0f;
    lr.name = "45Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ true));

    lr.desiredRefreshRate = 30.0f;
    lr.name = "30Hz Heuristic";
    EXPECT_EQ(expected30Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ true));

    lr.desiredRefreshRate = 24.0f;
    lr.name = "24Hz Heuristic";
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ true));

    lr.desiredRefreshRate = 24.0f;
    lr.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr.name = "24Hz ExplicitExactOrMultiple";
    EXPECT_EQ(expected72Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ true));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_PriorityTest) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_30, HWC_GROUP_ID_0, VSYNC_30},
             {HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.vote = LayerVoteType::Min;
    lr2.vote = LayerVoteType::Max;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Min;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Min;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 24.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Max;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Max;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 15.0f;
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 30.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 45.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_24FpsVideo) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::ExplicitExactOrMultiple;
    for (float fps = 23.0f; fps < 25.0f; fps += 0.1f) {
        lr.desiredRefreshRate = fps;
        const auto& refreshRate =
                refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false);
        printf("%.2fHz chooses %s\n", fps, refreshRate.name.c_str());
        EXPECT_EQ(expected60Config, refreshRate);
    }
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_getRefreshRateForContent_Explicit) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 90.0f;
    EXPECT_EQ(expected90Config, refreshRateConfigs->getRefreshRateForContent(layers));

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config, refreshRateConfigs->getRefreshRateForContent(layers));
}

TEST_F(RefreshRateConfigsTest, twoDeviceConfigs_getRefreshRateForContentV2_Explicit) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 60.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 90.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::ExplicitDefault;
    lr1.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::Heuristic;
    lr1.desiredRefreshRate = 90.0f;
    lr2.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr2.desiredRefreshRate = 60.0f;
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, testInPolicy) {
    RefreshRate expectedDefaultConfig = {HWC_CONFIG_ID_60, VSYNC_60_POINT_4, HWC_GROUP_ID_0,
                                         "60fps", 60};
    ASSERT_TRUE(expectedDefaultConfig.inPolicy(60.000004f, 60.000004f));
    ASSERT_TRUE(expectedDefaultConfig.inPolicy(59.0f, 60.1f));
    ASSERT_FALSE(expectedDefaultConfig.inPolicy(75.0f, 90.0f));
    ASSERT_FALSE(expectedDefaultConfig.inPolicy(60.0011f, 90.0f));
    ASSERT_FALSE(expectedDefaultConfig.inPolicy(50.0f, 59.998f));
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_75HzContent) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected30Config = {HWC_CONFIG_ID_30, VSYNC_30, HWC_GROUP_ID_0, "30fps", 30};
    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f}};
    auto& lr = layers[0];

    lr.vote = LayerVoteType::ExplicitExactOrMultiple;
    for (float fps = 75.0f; fps < 100.0f; fps += 0.1f) {
        lr.desiredRefreshRate = fps;
        const auto& refreshRate =
                refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false);
        printf("%.2fHz chooses %s\n", fps, refreshRate.name.c_str());
        EXPECT_EQ(expected90Config, refreshRate);
    }
}

TEST_F(RefreshRateConfigsTest, getRefreshRateForContentV2_Multiples) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 90.0f;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::ExplicitDefault;
    lr2.desiredRefreshRate = 90.0f;
    lr2.name = "90Hz ExplicitDefault";
    EXPECT_EQ(expected60Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Max;
    lr2.name = "Max";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 30.0f;
    lr1.name = "30Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 90.0f;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 30.0f;
    lr1.name = "30Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Max;
    lr2.name = "Max";
    EXPECT_EQ(expected90Config,
              refreshRateConfigs->getRefreshRateForContentV2(layers, /*touchActive*/ false));
}

TEST_F(RefreshRateConfigsTest, scrollWhileWatching60fps_60_90) {
    std::vector<RefreshRateConfigs::InputConfig> configs{
            {{HWC_CONFIG_ID_60, HWC_GROUP_ID_0, VSYNC_60},
             {HWC_CONFIG_ID_90, HWC_GROUP_ID_0, VSYNC_90}}};
    auto refreshRateConfigs =
            std::make_unique<RefreshRateConfigs>(configs, /*currentConfigId=*/HWC_CONFIG_ID_60);

    RefreshRate expected60Config = {HWC_CONFIG_ID_60, VSYNC_60, HWC_GROUP_ID_0, "60fps", 60};
    RefreshRate expected90Config = {HWC_CONFIG_ID_90, VSYNC_90, HWC_GROUP_ID_0, "90fps", 90};

    auto layers = std::vector<LayerRequirement>{LayerRequirement{.weight = 1.0f},
                                                LayerRequirement{.weight = 1.0f}};
    auto& lr1 = layers[0];
    auto& lr2 = layers[1];

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::NoVote;
    lr2.name = "NoVote";
    EXPECT_EQ(expected60Config, refreshRateConfigs->getRefreshRateForContentV2(layers, false));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::NoVote;
    lr2.name = "NoVote";
    EXPECT_EQ(expected90Config, refreshRateConfigs->getRefreshRateForContentV2(layers, true));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Max;
    lr2.name = "Max";
    EXPECT_EQ(expected90Config, refreshRateConfigs->getRefreshRateForContentV2(layers, true));

    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Max;
    lr2.name = "Max";
    EXPECT_EQ(expected90Config, refreshRateConfigs->getRefreshRateForContentV2(layers, false));

    // The other layer starts to provide buffers
    lr1.vote = LayerVoteType::ExplicitExactOrMultiple;
    lr1.desiredRefreshRate = 60.0f;
    lr1.name = "60Hz ExplicitExactOrMultiple";
    lr2.vote = LayerVoteType::Heuristic;
    lr2.desiredRefreshRate = 90.0f;
    lr2.name = "90Hz Heuristic";
    EXPECT_EQ(expected90Config, refreshRateConfigs->getRefreshRateForContentV2(layers, false));
}

} // namespace
} // namespace scheduler
} // namespace android
