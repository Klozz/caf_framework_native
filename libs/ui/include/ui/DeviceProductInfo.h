/*
 * Copyright 2020 The Android Open Source Project
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

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

namespace android {

// NUL-terminated plug and play ID.
using PnpId = std::array<char, 4>;

// Product-specific information about the display or the directly connected device on the
// display chain. For example, if the display is transitively connected, this field may contain
// product information about the intermediate device.
struct DeviceProductInfo {
    static constexpr size_t TEXT_BUFFER_SIZE = 20;

    struct ModelYear {
        uint32_t year;
    };

    struct ManufactureYear : ModelYear {};

    struct ManufactureWeekAndYear : ManufactureYear {
        // 1-base week number. Week numbering may not be consistent between manufacturers.
        uint8_t week;
    };

    // Display name.
    std::array<char, TEXT_BUFFER_SIZE> name;

    // Manufacturer Plug and Play ID.
    PnpId manufacturerPnpId;

    // Manufacturer product ID.
    std::array<char, TEXT_BUFFER_SIZE> productId;

    using ManufactureOrModelDate = std::variant<ModelYear, ManufactureYear, ManufactureWeekAndYear>;
    ManufactureOrModelDate manufactureOrModelDate;
};

} // namespace android
