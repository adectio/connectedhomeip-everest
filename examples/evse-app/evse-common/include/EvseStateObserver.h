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

#include <clusters/EnergyEvse/Enums.h>

#include <cstdint>

namespace chip {
namespace app {
namespace Clusters {
namespace EnergyEvse {

/**
 * Receives EVSE changes made through Matter, e.g. to forward them to an external charging stack.
 *
 * Register with EVSEManufacturer::SetStateObserver(). The methods are called from the EVSE
 * delegate's application callback, so implementations must not block.
 */
class EvseStateObserver
{
public:
    virtual ~EvseStateObserver() = default;

    virtual void OnStateChanged(StateEnum state, SupplyStateEnum supplyState) = 0;

    /** @param maximumChargeCurrent  Maximum charge current in mA */
    virtual void OnMaximumChargeCurrentChanged(int64_t maximumChargeCurrent) = 0;
};

} // namespace EnergyEvse
} // namespace Clusters
} // namespace app
} // namespace chip
