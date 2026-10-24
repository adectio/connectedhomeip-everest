/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <optional>
#include <string_view>

#include <clusters/EnergyEvse/Enums.h>

namespace everest::mqtt {

std::optional<chip::app::Clusters::EnergyEvse::FaultStateEnum> MatterFaultForEVerestError(std::string_view errorType);

int MatterFaultPriority(chip::app::Clusters::EnergyEvse::FaultStateEnum fault);

} // namespace everest::mqtt
