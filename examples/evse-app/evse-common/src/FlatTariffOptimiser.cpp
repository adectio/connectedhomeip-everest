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

#include <ChargingScheduleOptimiser.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include <algorithm>
#include <limits>

namespace chip {
namespace app {
namespace Clusters {
namespace EnergyEvse {

CHIP_ERROR FlatTariffOptimiser::ComputeSchedule(const ChargingScheduleRequest & request, ChargingSchedule & schedule)
{
    schedule.Clear();

    if (request.requiredEnergy_mWh <= 0)
    {
        // If we don't need to charge, then ensure we do not set a NextStartTime
        // (spec says this should be NULL if we don't plan to schedule a charge)
        return CHIP_NO_ERROR;
    }

    // Compute power to the nearest Watt from mV * mA to help avoid overflow
    const uint64_t power_W =
        static_cast<uint64_t>(std::max<int64_t>(0, (request.nominalVoltage_mV * request.maximumChargeCurrent_mA) / 1000000));
    if (power_W == 0)
    {
        ChipLogError(AppServer, "EVSE Error: MaxCurrent = 0Amp - Can't schedule charging");
        return CHIP_ERROR_INTERNAL;
    }

    if (request.chargeUntilFull)
    {
        // Nothing to plan around: start now and keep charging (until the EV stops demanding) up to the target
        const uint32_t duration_s =
            (request.targetTime_epoch_s > request.now_epoch_s) ? request.targetTime_epoch_s - request.now_epoch_s : kChargingLeeway_s;
        schedule.startTime_epoch_s.SetNonNull(request.now_epoch_s);
        return schedule.AddSlot(request.now_epoch_s, duration_s, request.maximumChargeCurrent_mA, request.nominalVoltage_mV);
    }

    // Time to charge(seconds) = (3600 * Energy(mWh) / Power(W)) / 1000
    // to avoid using floats we multiply by 36 and then divide by 10 (instead of x3600 and dividing by 1000)
    const uint64_t chargingDuration_s =
        (static_cast<uint64_t>(request.requiredEnergy_mWh) * 36) / (power_W * 10) + kChargingLeeway_s;

    const uint32_t clampedDuration_s =
        static_cast<uint32_t>(std::min<uint64_t>(chargingDuration_s, std::numeric_limits<uint32_t>::max()));

    // Start as late as possible; if there isn't enough time left to reach the target, start now
    uint32_t start_epoch_s = request.now_epoch_s;
    if (request.targetTime_epoch_s > request.now_epoch_s &&
        chargingDuration_s < static_cast<uint64_t>(request.targetTime_epoch_s - request.now_epoch_s))
    {
        start_epoch_s = request.targetTime_epoch_s - static_cast<uint32_t>(chargingDuration_s);
    }

    schedule.startTime_epoch_s.SetNonNull(start_epoch_s);
    return schedule.AddSlot(start_epoch_s, clampedDuration_s, request.maximumChargeCurrent_mA, request.nominalVoltage_mV);
}

} // namespace EnergyEvse
} // namespace Clusters
} // namespace app
} // namespace chip
