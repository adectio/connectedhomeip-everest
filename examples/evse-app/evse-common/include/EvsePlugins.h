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

#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>

namespace chip {
namespace app {
namespace Clusters {
namespace EnergyEvse {

/**
 * An optional extension linked into the EVSE app, e.g. an alternative ChargingScheduleOptimiser.
 *
 * Plugins are added to the build with the `evse_app_plugins` GN argument and register themselves from a static
 * initialiser, e.g.:
 *
 *     static MyPlugin sPlugin;
 *     static EvsePluginRegistration sRegistration(sPlugin);
 *
 * The app initialises registered plugins once the EVSE clusters and EVSEManufacturer are set up.
 */
class EvsePlugin
{
public:
    virtual ~EvsePlugin() = default;

    virtual const char * Name() const = 0;
    virtual CHIP_ERROR Init(EndpointId evseEndpoint) = 0;
    virtual void Shutdown() {}

private:
    friend void RegisterEvsePlugin(EvsePlugin & plugin);
    friend void InitEvsePlugins(EndpointId evseEndpoint);
    friend void ShutdownEvsePlugins();
    EvsePlugin * mNext = nullptr;
};

void RegisterEvsePlugin(EvsePlugin & plugin);

/** Registers a plugin when constructed (use as a static object next to the plugin) */
struct EvsePluginRegistration
{
    explicit EvsePluginRegistration(EvsePlugin & plugin) { RegisterEvsePlugin(plugin); }
};

/** Initialise every registered plugin; a failing plugin is logged and skipped */
void InitEvsePlugins(EndpointId evseEndpoint);
void ShutdownEvsePlugins();

} // namespace EnergyEvse
} // namespace Clusters
} // namespace app
} // namespace chip
