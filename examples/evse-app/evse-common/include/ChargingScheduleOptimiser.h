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

#include <app/data-model/Nullable.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CodeUtils.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace chip {
namespace app {
namespace Clusters {
namespace EnergyEvse {

/** Matches the maximum number of slots in a Device Energy Management forecast */
constexpr size_t kMaxChargingScheduleSlots = 10;

/**
 * Spare time left between the end of the planned charging and the target, for slow starting vehicles
 * (e.g. battery conditioning). It is not planned as extra energy, so the scheduled energy doesn't depend
 * on the charging current.
 */
constexpr uint32_t kChargingLeeway_s = 15 * 60;

/** Inputs for computing a charging schedule towards the next charging target */
struct ChargingScheduleRequest
{
    uint32_t now_epoch_s        = 0;
    uint32_t targetTime_epoch_s = 0;
    int64_t requiredEnergy_mWh  = 0;
    int64_t nominalVoltage_mV   = 0;
    int64_t minimumChargeCurrent_mA = 0;
    int64_t maximumChargeCurrent_mA = 0;
    /**
     * The required energy is unknown (e.g. a TargetSoC without AddedEnergy and the EV doesn't report its SoC), so the
     * EV should be charged until it stops demanding - requiredEnergy_mWh is then only a placeholder.
     */
    bool chargeUntilFull = false;
};

/** A cost in a currency, e.g. a value of 15230 with 4 decimal points in GBP (826) is £1.5230 */
struct ChargingSlotCost
{
    int64_t value         = 0;
    uint8_t decimalPoints = 0;
    uint16_t currency     = 0;
};

/**
 * A contiguous period of the schedule. A chargeCurrent_mA of 0 is a period in which
 * the EVSE does not charge (e.g. waiting for a cheaper tariff period).
 *
 * The costs are only known by optimisers with tariff / grid carbon data, and are published
 * as the slot's Device Energy Management Costs.
 */
struct ChargingScheduleSlot
{
    uint32_t start_epoch_s   = 0;
    uint32_t duration_s      = 0;
    int64_t chargeCurrent_mA = 0;
    int64_t power_mW         = 0;
    std::optional<ChargingSlotCost> financialCost;
    /** Grid greenhouse gas emissions of the energy in this slot, in grams CO2e */
    std::optional<int64_t> ghgEmissions_g;
};

/**
 * The output of a ChargingScheduleOptimiser.
 *
 * startTime_epoch_s is null when no charging is planned. Slots start at startTime_epoch_s and
 * are contiguous, so they can be mapped directly onto Device Energy Management forecast slots.
 */
struct ChargingSchedule
{
    DataModel::Nullable<uint32_t> startTime_epoch_s;
    std::array<ChargingScheduleSlot, kMaxChargingScheduleSlots> slots;
    size_t slotCount = 0;

    void Clear()
    {
        startTime_epoch_s.SetNull();
        slotCount = 0;
    }

    CHIP_ERROR AddSlot(uint32_t start_epoch_s, uint32_t duration_s, int64_t chargeCurrent_mA, int64_t nominalVoltage_mV)
    {
        VerifyOrReturnError(slotCount < slots.size(), CHIP_ERROR_NO_MEMORY);
        ChargingScheduleSlot & slot = slots[slotCount++];
        slot                        = ChargingScheduleSlot();
        slot.start_epoch_s          = start_epoch_s;
        slot.duration_s             = duration_s;
        slot.chargeCurrent_mA       = chargeCurrent_mA;
        slot.power_mW               = (chargeCurrent_mA * nominalVoltage_mV) / 1000;
        return CHIP_NO_ERROR;
    }
};

/**
 * Strategy used by the EVSEManufacturer to decide when (and at what current) to charge.
 */
class ChargingScheduleOptimiser
{
public:
    virtual ~ChargingScheduleOptimiser() = default;

    virtual CHIP_ERROR ComputeSchedule(const ChargingScheduleRequest & request, ChargingSchedule & schedule) = 0;
};

/**
 * Simple optimiser that assumes a flat tariff throughout the day, so it charges at the
 * maximum current as late as possible while still finishing kChargingLeeway_s before the target.
 *
 * When the energy needed is unknown it charges from now until the target time.
 */
class FlatTariffOptimiser : public ChargingScheduleOptimiser
{
public:
    CHIP_ERROR ComputeSchedule(const ChargingScheduleRequest & request, ChargingSchedule & schedule) override;
};

} // namespace EnergyEvse
} // namespace Clusters
} // namespace app
} // namespace chip
