/*
 *
 *    Copyright (c) 2023-2024 Project CHIP Authors
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

#include <AppMain.h>
#include <EnergyEvseMain.h>
#include <EnergyManagementAppCmdLineOptions.h>
#include <EverestMqttThread.h>
#include <Identify.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/support/BitMask.h>

#include <memory>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::DeviceEnergyManagement;
using namespace chip::app::Clusters::DeviceEnergyManagement::Attributes;

// Parse a hex (prefixed by 0x) or decimal (no-prefix) string
static uint32_t ParseNumber(const char * pString);

// Parses the --featureMap option
static bool EnergyAppOptionHandler(const char * aProgram, chip::ArgParser::OptionSet * aOptions, int aIdentifier,
                                   const char * aName, const char * aValue);

constexpr uint16_t kOptionFeatureMap           = 0xffd1;
constexpr uint16_t kOptionErrorHistoryModuleId = 0xffd2;
constexpr uint16_t kOptionErrorOriginModuleId  = 0xffd3;

constexpr chip::EndpointId kEvseEndpoint = 1;

namespace {
std::unique_ptr<EverestMqttThread> gEverestMqttThread;
EverestMqttThread::Config gEverestMqttConfig;
} // namespace

EverestMqttThread * GetEverestMqttThread()
{
    return gEverestMqttThread.get();
}

// Define the chip::ArgParser command line structures for extending the command line to support the
// energy apps
static chip::ArgParser::OptionDef sEnergyAppOptionDefs[] = {
    { "featureSet", chip::ArgParser::kArgumentRequired, kOptionFeatureMap },
    { "everest-error-history-module-id", chip::ArgParser::kArgumentRequired, kOptionErrorHistoryModuleId },
    { "everest-error-origin-module-id", chip::ArgParser::kArgumentRequired, kOptionErrorOriginModuleId },
    { nullptr }
};

static chip::ArgParser::OptionSet sCmdLineOptions = { EnergyAppOptionHandler, // handler function
                                                      sEnergyAppOptionDefs,   // array of option definitions
                                                      "PROGRAM OPTIONS",      // help group
                                                      "-f, --featureSet <value>\n"
                                                      "    --everest-error-history-module-id <module-id>\n"
                                                      "    --everest-error-origin-module-id <module-id>\n" };

namespace chip {
namespace app {
namespace Clusters {
namespace DeviceEnergyManagement {

// Keep track of the parsed featureMap option
static chip::BitMask<Feature> sFeatureMap(Feature::kPowerAdjustment, Feature::kPowerForecastReporting,
                                          Feature::kStartTimeAdjustment, Feature::kPausable, Feature::kForecastAdjustment,
                                          Feature::kConstraintBasedAdjustment);

chip::BitMask<Feature> GetFeatureMapFromCmdLine()
{
    return sFeatureMap;
}

} // namespace DeviceEnergyManagement
} // namespace Clusters
} // namespace app
} // namespace chip

chip::EndpointId GetEnergyDeviceEndpointId()
{
    return kEvseEndpoint;
}

static uint32_t ParseNumber(const char * pString)
{
    uint32_t num = 0;
    if (strlen(pString) > 2 && pString[0] == '0' && pString[1] == 'x')
    {
        num = (uint32_t) strtoul(&pString[2], nullptr, 16);
    }
    else
    {
        num = (uint32_t) strtoul(pString, nullptr, 10);
    }

    return num;
}

void ApplicationInit()
{
    ChipLogDetail(AppServer, "EVSE App: ApplicationInit()");
    SuccessOrDie(IdentifyInit());
    EvseApplicationInit();
    gEverestMqttThread = std::make_unique<EverestMqttThread>(gEverestMqttConfig);
    gEverestMqttThread->Start();
}

void ApplicationShutdown()
{
    ChipLogDetail(AppServer, "EVSE App: ApplicationShutdown()");
    if (gEverestMqttThread)
    {
        gEverestMqttThread->Stop();
        gEverestMqttThread.reset();
    }
    EvseApplicationShutdown();
}

static bool EnergyAppOptionHandler(const char * aProgram, chip::ArgParser::OptionSet * aOptions, int aIdentifier,
                                   const char * aName, const char * aValue)
{
    bool retval = true;

    switch (aIdentifier)
    {
    case kOptionFeatureMap:
        sFeatureMap = BitMask<chip::app::Clusters::DeviceEnergyManagement::Feature>(ParseNumber(aValue));
        ChipLogDetail(Support, "Using FeatureMap 0x%04x", sFeatureMap.Raw());
        break;
    case kOptionErrorHistoryModuleId:
        gEverestMqttConfig.errorHistoryModuleId = aValue;
        break;
    case kOptionErrorOriginModuleId:
        gEverestMqttConfig.errorOriginModuleId = aValue;
        break;
    default:
        ChipLogError(Support, "%s: INTERNAL ERROR: Unhandled option: %s\n", aProgram, aName);
        retval = false;
        break;
    }

    return (retval);
}

int main(int argc, char * argv[])
{
    if (ChipLinuxAppInit(argc, argv, &sCmdLineOptions) != 0)
    {
        return -1;
    }

    ChipLinuxAppMainLoop();

    return 0;
}
