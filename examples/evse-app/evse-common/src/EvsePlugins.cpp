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

#include <EvsePlugins.h>

#include <lib/support/logging/CHIPLogging.h>

namespace chip {
namespace app {
namespace Clusters {
namespace EnergyEvse {

namespace {

// A function-local static so that registration from other translation units' static initialisers is safe
EvsePlugin *& RegisteredPlugins()
{
    static EvsePlugin * sHead = nullptr;
    return sHead;
}

} // namespace

void RegisterEvsePlugin(EvsePlugin & plugin)
{
    plugin.mNext        = RegisteredPlugins();
    RegisteredPlugins() = &plugin;
}

void InitEvsePlugins(EndpointId evseEndpoint)
{
    for (EvsePlugin * plugin = RegisteredPlugins(); plugin != nullptr; plugin = plugin->mNext)
    {
        CHIP_ERROR err = plugin->Init(evseEndpoint);
        if (err == CHIP_NO_ERROR)
        {
            ChipLogProgress(AppServer, "EVSE plugin %s initialised", plugin->Name());
        }
        else
        {
            ChipLogError(AppServer, "EVSE plugin %s failed to initialise: %" CHIP_ERROR_FORMAT, plugin->Name(), err.Format());
        }
    }
}

void ShutdownEvsePlugins()
{
    for (EvsePlugin * plugin = RegisteredPlugins(); plugin != nullptr; plugin = plugin->mNext)
    {
        plugin->Shutdown();
    }
}

} // namespace EnergyEvse
} // namespace Clusters
} // namespace app
} // namespace chip
