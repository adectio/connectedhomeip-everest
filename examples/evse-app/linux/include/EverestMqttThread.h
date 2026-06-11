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

#include <atomic>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct mosquitto;

class EverestMqttThread
{
public:
    struct Config
    {
        std::string brokerHost          = "127.0.0.1";
        uint16_t brokerPort             = 1883;
        std::string clientId            = "matter-evse-linux";
        std::string everestPrefix       = "everest";
        std::string evseModuleId        = "connector_1";
        std::string evseImplementationId = "evse";
        std::chrono::seconds retryBackoff{ 5 };
    };

    enum class ConnectionState
    {
        Stopped,
        Disconnected,
        Connecting,
        Connected,
    };

    explicit EverestMqttThread(Config config);
    ~EverestMqttThread();

    void Start();
    void Stop();
    void RequestReconnect();

    ConnectionState GetConnectionState() const;

private:
    static void HandleConnect(struct mosquitto * mosq, void * obj, int rc);
    static void HandleDisconnect(struct mosquitto * mosq, void * obj, int rc);
    static void HandleMessage(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message);

    void ThreadMain();
    bool Connect();
    void Disconnect();
    bool EnsureClient();
    bool SubscribeTopics();
    std::string BuildVarTopic(const std::string & varName) const;
    void HandleMessage(const std::string & topic, const std::string & payload);
    void HandleHwCapabilitiesMessage(const std::string & payload);
    void HandleEvInfoMessage(const std::string & payload);
    void HandlePowermeterMessage(const std::string & payload);
    void HandleLimitsMessage(const std::string & payload);
    void HandleSessionEventMessage(const std::string & payload);

    Config mConfig;
    std::string mHwCapabilitiesTopic;
    std::string mEvInfoTopic;
    std::string mPowermeterTopic;
    std::string mLimitsTopic;
    std::string mSessionEventTopic;
    std::thread mThread;
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<ConnectionState> mConnectionState = ConnectionState::Stopped;
    bool mShouldStop                             = false;
    bool mReconnectRequested                     = false;
    struct mosquitto * mMosquitto                = nullptr;
    std::optional<int64_t> mLastHardwareMaxCurrentMilliAmps;
    std::optional<int64_t> mLastHardwareMaxDischargeCurrentMilliAmps;
    std::optional<int64_t> mLastCircuitCapacityMilliAmps;
    std::optional<int64_t> mLastNominalMainsVoltageMilliVolts;
    std::optional<int> mLastMatterEvseState;
    std::optional<uint8_t> mLastStateOfChargePercent;
    std::optional<int64_t> mLastBatteryCapacityMilliWattHours;
    std::optional<std::string> mLastVehicleId;
    std::optional<int64_t> mLastPowermeterImportMilliWattHours;
    std::optional<int64_t> mLastPowermeterExportMilliWattHours;
    std::optional<uint32_t> mCurrentSessionId;
    std::optional<std::chrono::steady_clock::time_point> mCurrentSessionStart;
    std::optional<int64_t> mSessionEnergyImportStartMilliWattHours;
    std::optional<int64_t> mSessionEnergyExportStartMilliWattHours;
};
