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

#include <string>

namespace everest::mqtt {

class EvseManagerApiTopics
{
public:
    static constexpr const char kPauseChargingCommand[]  = "pause_charging";
    static constexpr const char kResumeChargingCommand[] = "resume_charging";

    static constexpr const char kHwCapabilitiesVariable[] = "hw_capabilities";
    static constexpr const char kEvInfoVariable[]         = "ev_info";
    static constexpr const char kPowermeterVariable[]     = "powermeter";
    static constexpr const char kEnforcedLimitsVariable[] = "enforced_limits";
    static constexpr const char kSessionEventVariable[]   = "session_event";
    static constexpr const char kSessionInfoVariable[]    = "session_info";

    EvseManagerApiTopics(const std::string & apiModuleId, const std::string & clientId) :
        mApiBase("everest_api/1/evse_manager_consumer/" + apiModuleId),
        mClientId(clientId)
    {}

    std::string Command(const char * command) const { return mApiBase + "/m2e/" + command; }

    std::string CommandReply(const char * command) const { return mApiBase + "/m2e/reply/" + mClientId + "/" + command; }

    std::string Variable(const char * variable) const { return mApiBase + "/e2m/" + variable; }

private:
    std::string mApiBase;
    std::string mClientId;
};

} // namespace everest::mqtt
