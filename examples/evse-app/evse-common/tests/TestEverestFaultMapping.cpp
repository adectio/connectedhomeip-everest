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
#include <gtest/gtest.h>

using chip::app::Clusters::EnergyEvse::FaultStateEnum;

TEST(TestEverestFaultMapping, MapsDocumentedErrors)
{
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("evse_board_support/MREC2GroundFailure"), FaultStateEnum::kGroundFault);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("ac_rcd/AC"), FaultStateEnum::kGroundFault);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC4OverCurrentFailure"), FaultStateEnum::kOverCurrent);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC5OverVoltage"), FaultStateEnum::kOverVoltage);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC6UnderVoltage"), FaultStateEnum::kUnderVoltage);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC8EmergencyStop"), FaultStateEnum::kEmergencyStop);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC15PowerLoss"), FaultStateEnum::kPowerLoss);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC18CableOverTempDerate"), FaultStateEnum::kOverTemperature);
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("MREC17EVSEContactorFault"), FaultStateEnum::kOther);
}

TEST(TestEverestFaultMapping, IgnoresWarningsAndMapsUnknownErrorsConservatively)
{
    EXPECT_FALSE(everest::mqtt::MatterFaultForEVerestError("VendorWarning").has_value());
    EXPECT_EQ(everest::mqtt::MatterFaultForEVerestError("unrecognized/Error"), FaultStateEnum::kOther);
}

TEST(TestEverestFaultMapping, UsesDocumentedFaultPrecedence)
{
    EXPECT_LT(everest::mqtt::MatterFaultPriority(FaultStateEnum::kGroundFault),
              everest::mqtt::MatterFaultPriority(FaultStateEnum::kOverTemperature));
    EXPECT_LT(everest::mqtt::MatterFaultPriority(FaultStateEnum::kOverTemperature),
              everest::mqtt::MatterFaultPriority(FaultStateEnum::kOther));
}
