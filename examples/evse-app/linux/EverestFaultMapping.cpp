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

#include <EverestFaultMapping.h>

namespace everest::mqtt {

std::optional<chip::app::Clusters::EnergyEvse::FaultStateEnum> MatterFaultForEVerestError(std::string_view errorType)
{
    const size_t separator           = errorType.rfind('/');
    const std::string_view errorName = (separator == std::string_view::npos) ? errorType : errorType.substr(separator + 1);

    using FaultStateEnum = chip::app::Clusters::EnergyEvse::FaultStateEnum;

    if (errorName == "VendorWarning")
    {
        return std::nullopt;
    }
    if (errorName == "MREC2GroundFailure" || errorName == "AC" || errorName == "DC")
    {
        return FaultStateEnum::kGroundFault;
    }
    if (errorName == "MREC4OverCurrentFailure")
    {
        return FaultStateEnum::kOverCurrent;
    }
    if (errorName == "MREC5OverVoltage" || errorName == "MREC24ConnectorVoltageHigh")
    {
        return FaultStateEnum::kOverVoltage;
    }
    if (errorName == "MREC6UnderVoltage")
    {
        return FaultStateEnum::kUnderVoltage;
    }
    if (errorName == "MREC8EmergencyStop")
    {
        return FaultStateEnum::kEmergencyStop;
    }
    if (errorName == "MREC15PowerLoss" || errorName == "BrownOut")
    {
        return FaultStateEnum::kPowerLoss;
    }
    if (errorName == "MREC3HighTemperature" || errorName == "MREC18CableOverTempDerate" || errorName == "MREC19CableOverTempStop")
    {
        return FaultStateEnum::kOverTemperature;
    }

    return FaultStateEnum::kOther;
}

int MatterFaultPriority(chip::app::Clusters::EnergyEvse::FaultStateEnum fault)
{
    using FaultStateEnum = chip::app::Clusters::EnergyEvse::FaultStateEnum;

    switch (fault)
    {
    case FaultStateEnum::kGroundFault:
        return 0;
    case FaultStateEnum::kOverCurrent:
        return 1;
    case FaultStateEnum::kOverVoltage:
        return 2;
    case FaultStateEnum::kUnderVoltage:
        return 3;
    case FaultStateEnum::kEmergencyStop:
        return 4;
    case FaultStateEnum::kPowerLoss:
        return 5;
    case FaultStateEnum::kOverTemperature:
        return 6;
    case FaultStateEnum::kOther:
        return 7;
    case FaultStateEnum::kNoError:
    default:
        return 8;
    }
}

} // namespace everest::mqtt
