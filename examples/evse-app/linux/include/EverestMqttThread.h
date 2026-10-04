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

#include <EverestEvseManagerApiTopics.h>
#include <EverestFaultMapping.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <clusters/EnergyEvse/Enums.h>

struct mosquitto;

class EverestMqttThread
{
public:
    struct Config
    {
        std::string brokerHost           = "127.0.0.1";
        uint16_t brokerPort              = 1883;
        std::string clientId             = "matter-evse-linux";
        std::string apiModuleId          = "evse_manager_api";
        std::string errorHistoryModuleId = "error_history_1";
        std::string errorOriginModuleId  = "bsp_1";
        std::string externalEnergyLimitsModuleId;
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
    void HandleMatterStateChange(chip::app::Clusters::EnergyEvse::StateEnum state,
                                 chip::app::Clusters::EnergyEvse::SupplyStateEnum supplyState);
    void HandleMatterMaximumChargeCurrentChange(int64_t maximumChargeCurrent);

private:
    enum class CommandSelector
    {
        Unknown,
        PauseChargingResponse,
        ResumeChargingResponse,
    };

    enum class ExternalEnergyLimitsState
    {
        Disabled,
        AwaitingBaselineCapacity,
        ApplyingBaselineCapacity,
        Active,
    };

    struct PendingCommand
    {
        bool completed = false;
        std::optional<bool> expectedRetval;
        std::optional<bool> retval;
        std::string error;
    };

    struct PendingFaultMessage
    {
        bool raised;
        std::string payload;
    };

    static void HandleConnect(struct mosquitto * mosq, void * obj, int rc);
    static void HandleDisconnect(struct mosquitto * mosq, void * obj, int rc);
    static void HandleMessage(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message);
    static void SynchronizeMatterSupplyState(intptr_t context);
    static void CompleteExternalEnergyLimitsBootstrap(intptr_t context);

    void ThreadMain();
    bool Connect();
    void Disconnect();
    bool EnsureClient();
    bool SubscribeTopics();
    CommandSelector SelectCommandTopic(const std::string & topic) const;
    void HandleMessage(const std::string & topic, const std::string & payload);
    void HandleCommandResponse(const std::string & responseTopic, const std::string & payload);
    void HandleHwCapabilitiesMessage(const std::string & payload);
    void HandleEvInfoMessage(const std::string & payload);
    void HandlePowermeterMessage(const std::string & payload);
    void HandleLimitsMessage(const std::string & payload);
    void HandleAcPpAmpacityMessage(const std::string & payload);
    void HandleSessionEventMessage(const std::string & payload);
    void HandleSessionInfoMessage(const std::string & payload);
    void HandleActiveErrorsMessage(const std::string & payload);
    void HandleErrorRaisedMessage(const std::string & payload);
    void HandleErrorClearedMessage(const std::string & payload);
    void ProcessErrorRaisedMessage(const std::string & payload);
    void ProcessErrorClearedMessage(const std::string & payload);
    bool RequestActiveErrors();
    void UpdateMatterFault();
    bool SendPauseChargingCommand();
    bool SendResumeChargingCommand();
    bool PublishMaximumChargeCurrent(int64_t maximumChargeCurrent);
    bool PublishUnconstrainedExternalLimits();
    bool SendEVerestCommand(const std::string & cmdName, const std::string & responseTopic, std::optional<bool> expectedRetval);

    Config mConfig;
    everest::mqtt::EvseManagerApiTopics mApiTopics;
    everest::mqtt::ErrorHistoryConsumerApiTopics mErrorHistoryApiTopics;
    everest::mqtt::ExternalEnergyLimitsApiTopics mExternalEnergyLimitsApiTopics;
    std::string mPauseChargingResponseTopic;
    std::string mResumeChargingResponseTopic;
    std::string mHwCapabilitiesTopic;
    std::string mEvInfoTopic;
    std::string mPowermeterTopic;
    std::string mLimitsTopic;
    std::string mAcPpAmpacityTopic;
    std::string mSessionEventTopic;
    std::string mSessionInfoTopic;
    std::string mActiveErrorsResponseTopic;
    std::string mErrorRaisedTopic;
    std::string mErrorClearedTopic;
    std::string mSetExternalLimitsTopic;
    std::thread mThread;
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<ConnectionState> mConnectionState = ConnectionState::Stopped;
    bool mShouldStop                              = false;
    bool mReconnectRequested                      = false;
    struct mosquitto * mMosquitto                 = nullptr;
    std::optional<int64_t> mLastHardwareMaxCurrentMilliAmps;
    std::optional<int64_t> mLastHardwareMaxDischargeCurrentMilliAmps;
    std::optional<int64_t> mLastCircuitCapacityMilliAmps;
    std::optional<int64_t> mProximityPilotCableAmpacityMilliAmps;
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
    std::optional<int> mLastForwardedSupplyState;
    std::atomic<ExternalEnergyLimitsState> mExternalEnergyLimitsState = ExternalEnergyLimitsState::Disabled;
    bool mActiveErrorsSeeded                                          = false;
    std::vector<PendingFaultMessage> mPendingFaultMessages;
    std::unordered_map<std::string, chip::app::Clusters::EnergyEvse::FaultStateEnum> mActiveFaults;
    chip::app::Clusters::EnergyEvse::FaultStateEnum mSelectedMatterFault =
        chip::app::Clusters::EnergyEvse::FaultStateEnum::kNoError;
    std::mutex mCommandMutex;
    std::condition_variable mCommandCondition;
    std::unordered_map<std::string, PendingCommand> mPendingCommands;
};

EverestMqttThread * GetEverestMqttThread();
