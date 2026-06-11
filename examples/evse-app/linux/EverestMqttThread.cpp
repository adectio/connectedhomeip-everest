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

#include "EverestMqttThread.h"

#include <EVSEManufacturerImpl.h>
#include <json/json.h>
#include <lib/support/logging/CHIPLogging.h>
#include <mosquitto.h>
#include <platform/PlatformManager.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {
using chip::DeviceLayer::PlatformMgr;
using chip::Percent;
using chip::app::DataModel::MakeNullable;
using chip::app::Clusters::EnergyEvse::StateEnum;

constexpr const char * kLogModule       = "EverestMQTT";
constexpr int kKeepAliveSeconds         = 60;
constexpr int kLoopTimeoutMs            = 1000;
constexpr int kReconnectDelaySeconds    = 1;
constexpr int kReconnectDelayMaxSeconds = 5;

struct MatterEvseUpdate
{
    std::optional<int64_t> maxHardwareChargeCurrentLimitMilliAmps;
    std::optional<int64_t> maxHardwareDischargeCurrentLimitMilliAmps;
    std::optional<int64_t> nominalMainsVoltageMilliVolts;
    std::optional<int64_t> circuitCapacityMilliAmps;
    std::optional<StateEnum> evseState;
    std::optional<uint8_t> stateOfChargePercent;
    bool clearStateOfCharge = false;
    std::optional<int64_t> batteryCapacityMilliWattHours;
    bool clearBatteryCapacity = false;
    std::optional<std::string> vehicleId;
    bool clearVehicleId = false;
    std::optional<uint32_t> sessionId;
    std::optional<uint32_t> sessionDurationSeconds;
    std::optional<int64_t> sessionEnergyChargedMilliWattHours;
    std::optional<int64_t> sessionEnergyDischargedMilliWattHours;
};

enum class TopicSelector
{
    kUnknown,
    kHwCapabilities,
    kEvInfo,
    kPowermeter,
    kLimits,
    kSessionEvent,
};

enum class SessionEventSelector
{
    kUnknown,
    kPluggedInNoDemand,
    kPluggedInDemand,
    kUnplugged,
};

class MatterEvseUpdateHandler
{
public:
    explicit MatterEvseUpdateHandler(MatterEvseUpdate update) : mUpdate(std::move(update)) {}

    static MatterEvseUpdateHandler * Create(MatterEvseUpdate update)
    {
        return chip::Platform::New<MatterEvseUpdateHandler>(std::move(update));
    }

    static void Apply(intptr_t context)
    {
        auto * self = reinterpret_cast<MatterEvseUpdateHandler *>(context);
        VerifyOrReturn(self != nullptr);

        auto * manufacturer = chip::app::Clusters::EnergyEvse::GetEvseManufacturer();
        if (manufacturer == nullptr)
        {
            ChipLogError(AppServer, "[%s] EVSE manufacturer is not initialized", kLogModule);
            chip::Platform::Delete(self);
            return;
        }

        auto * delegate = manufacturer->GetEvseDelegate();
        auto * instance = manufacturer->GetEvseInstance();
        if (delegate == nullptr || instance == nullptr)
        {
            ChipLogError(AppServer, "[%s] EVSE delegate or instance is not initialized", kLogModule);
            chip::Platform::Delete(self);
            return;
        }

        if (self->mUpdate.maxHardwareChargeCurrentLimitMilliAmps.has_value())
        {
            delegate->HwSetMaxHardwareChargeCurrentLimit(
                self->mUpdate.maxHardwareChargeCurrentLimitMilliAmps.value());
        }

        if (self->mUpdate.maxHardwareDischargeCurrentLimitMilliAmps.has_value())
        {
            delegate->HwSetMaxHardwareDischargeCurrentLimit(
                self->mUpdate.maxHardwareDischargeCurrentLimitMilliAmps.value());
        }

        if (self->mUpdate.nominalMainsVoltageMilliVolts.has_value())
        {
            delegate->HwSetNominalMainsVoltage(self->mUpdate.nominalMainsVoltageMilliVolts.value());
        }

        if (self->mUpdate.circuitCapacityMilliAmps.has_value())
        {
            delegate->HwSetCircuitCapacity(self->mUpdate.circuitCapacityMilliAmps.value());
        }

        if (self->mUpdate.evseState.has_value())
        {
            delegate->HwSetState(self->mUpdate.evseState.value());
        }

        if (self->mUpdate.clearStateOfCharge)
        {
            TEMPORARY_RETURN_IGNORED instance->SetStateOfCharge(chip::app::DataModel::NullNullable);
        }
        else if (self->mUpdate.stateOfChargePercent.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetStateOfCharge(
                MakeNullable(static_cast<Percent>(self->mUpdate.stateOfChargePercent.value())));
        }

        if (self->mUpdate.clearBatteryCapacity)
        {
            TEMPORARY_RETURN_IGNORED instance->SetBatteryCapacity(chip::app::DataModel::NullNullable);
        }
        else if (self->mUpdate.batteryCapacityMilliWattHours.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetBatteryCapacity(
                MakeNullable(self->mUpdate.batteryCapacityMilliWattHours.value()));
        }

        if (self->mUpdate.clearVehicleId)
        {
            delegate->HwSetVehicleID(chip::CharSpan("", 0));
        }
        else if (self->mUpdate.vehicleId.has_value())
        {
            const std::string & vehicleId = self->mUpdate.vehicleId.value();
            delegate->HwSetVehicleID(chip::CharSpan(vehicleId.data(), vehicleId.size()));
        }

        if (self->mUpdate.sessionId.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetSessionID(MakeNullable(self->mUpdate.sessionId.value()));
        }

        if (self->mUpdate.sessionDurationSeconds.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetSessionDuration(MakeNullable(self->mUpdate.sessionDurationSeconds.value()));
        }

        if (self->mUpdate.sessionEnergyChargedMilliWattHours.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetSessionEnergyCharged(
                MakeNullable(self->mUpdate.sessionEnergyChargedMilliWattHours.value()));
        }

        if (self->mUpdate.sessionEnergyDischargedMilliWattHours.has_value())
        {
            TEMPORARY_RETURN_IGNORED instance->SetSessionEnergyDischarged(
                MakeNullable(self->mUpdate.sessionEnergyDischargedMilliWattHours.value()));
        }

        chip::Platform::Delete(self);
    }
    MatterEvseUpdate mUpdate;
};

bool ParseEVerestVarPayload(const std::string & payload, Json::Value & data)
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::string parseErrors;
    std::istringstream stream(payload);
    if (!Json::parseFromStream(builder, stream, &data, &parseErrors))
    {
        ChipLogError(AppServer, "[%s] Failed to parse MQTT payload as JSON: %s", kLogModule, parseErrors.c_str());
        return false;
    }

    if (!data.isObject())
    {
        ChipLogError(AppServer, "[%s] Ignoring non-object MQTT payload", kLogModule);
        return false;
    }

    const Json::Value & msgType = data["msg_type"];
    if (msgType.isString())
    {
        const std::string msgTypeString = msgType.asString();
        if (msgTypeString != "Var" && msgTypeString != "var")
        {
            ChipLogError(AppServer, "[%s] Ignoring unexpected MQTT payload msg_type '%s'", kLogModule,
                         msgTypeString.c_str());
            return false;
        }

        const Json::Value & envelope = data["data"];
        if (envelope.isObject() && envelope.isMember("data"))
        {
            data = envelope["data"];
            return true;
        }

        data = envelope;
        return true;
    }

    if (!data.isMember("data"))
    {
        ChipLogError(AppServer, "[%s] Ignoring MQTT payload without variable data", kLogModule);
        return false;
    }

    data = data["data"];
    return true;
}

std::optional<int64_t> JsonCurrentToMilliAmps(const Json::Value & value)
{
    if (!value.isNumeric())
    {
        return std::nullopt;
    }

    const double milliAmps = value.asDouble() * 1000.0;
    if (!std::isfinite(milliAmps) || milliAmps < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        milliAmps > static_cast<double>(std::numeric_limits<int64_t>::max()))
    {
        return std::nullopt;
    }

    return static_cast<int64_t>(std::llround(milliAmps));
}

std::optional<int64_t> JsonEnergyToMilliWattHours(const Json::Value & value)
{
    if (!value.isObject() || !value["total"].isNumeric())
    {
        return std::nullopt;
    }

    const double milliWattHours = value["total"].asDouble() * 1000.0;
    if (!std::isfinite(milliWattHours) || milliWattHours < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        milliWattHours > static_cast<double>(std::numeric_limits<int64_t>::max()))
    {
        return std::nullopt;
    }

    return static_cast<int64_t>(std::llround(milliWattHours));
}

std::optional<int64_t> JsonVoltageToMilliVolts(const Json::Value & value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    constexpr const char * kVoltageKeys[] = { "L1", "DC", "L2", "L3" };
    for (const char * key : kVoltageKeys)
    {
        const Json::Value & member = value[key];
        if (!member.isNumeric())
        {
            continue;
        }

        const double milliVolts = member.asDouble() * 1000.0;
        if (!std::isfinite(milliVolts) || milliVolts < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
            milliVolts > static_cast<double>(std::numeric_limits<int64_t>::max()))
        {
            return std::nullopt;
        }

        return static_cast<int64_t>(std::llround(milliVolts));
    }

    return std::nullopt;
}

std::optional<uint8_t> JsonSocToPercent(const Json::Value & value)
{
    if (!value.isNumeric())
    {
        return std::nullopt;
    }

    const double soc = value.asDouble();
    if (!std::isfinite(soc))
    {
        return std::nullopt;
    }

    const long long rounded = std::llround(soc);
    return static_cast<uint8_t>(std::clamp<long long>(rounded, 0, 100));
}

uint32_t SessionUuidToMatterId(std::string_view uuid)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : uuid)
    {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

void ScheduleMatterUpdate(MatterEvseUpdate update)
{
    auto * handler = MatterEvseUpdateHandler::Create(std::move(update));
    VerifyOrReturn(handler != nullptr);
    CHIP_ERROR err = PlatformMgr().ScheduleWork(&MatterEvseUpdateHandler::Apply, reinterpret_cast<intptr_t>(handler));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "[%s] Failed to schedule Matter EVSE update: %" CHIP_ERROR_FORMAT, kLogModule,
                     err.Format());
        chip::Platform::Delete(handler);
    }
}

TopicSelector ParseTopicSelector(std::string_view topic)
{
    const size_t separator = topic.rfind('/');
    const std::string_view topicLeaf = (separator == std::string_view::npos) ? topic : topic.substr(separator + 1);

    static const std::unordered_map<std::string_view, TopicSelector> kTopicSelectors = {
        { "hw_capabilities", TopicSelector::kHwCapabilities },
        { "ev_info", TopicSelector::kEvInfo },
        { "powermeter", TopicSelector::kPowermeter },
        { "limits", TopicSelector::kLimits },
        { "session_event", TopicSelector::kSessionEvent },
    };

    const auto selector = kTopicSelectors.find(topicLeaf);
    return (selector != kTopicSelectors.end()) ? selector->second : TopicSelector::kUnknown;
}

SessionEventSelector ParseSessionEventSelector(std::string_view event)
{
    static const std::unordered_map<std::string_view, SessionEventSelector> kSessionSelectors = {
        { "SessionStarted", SessionEventSelector::kPluggedInNoDemand },
        { "SessionResumed", SessionEventSelector::kPluggedInNoDemand },
        { "AuthRequired", SessionEventSelector::kPluggedInNoDemand },
        { "PrepareCharging", SessionEventSelector::kPluggedInNoDemand },
        { "ChargingPausedEV", SessionEventSelector::kPluggedInNoDemand },
        { "ChargingFinished", SessionEventSelector::kPluggedInNoDemand },
        { "TransactionFinished", SessionEventSelector::kPluggedInNoDemand },
        { "PluginTimeout", SessionEventSelector::kPluggedInNoDemand },
        { "TransactionStarted", SessionEventSelector::kPluggedInDemand },
        { "ChargingStarted", SessionEventSelector::kPluggedInDemand },
        { "ChargingPausedEVSE", SessionEventSelector::kPluggedInDemand },
        { "StoppingCharging", SessionEventSelector::kPluggedInDemand },
        { "SwitchingPhases", SessionEventSelector::kPluggedInDemand },
        { "SessionFinished", SessionEventSelector::kUnplugged },
    };

    const auto selector = kSessionSelectors.find(event);
    return (selector != kSessionSelectors.end()) ? selector->second : SessionEventSelector::kUnknown;
}
} // namespace

EverestMqttThread::EverestMqttThread(Config config) :
    mConfig(std::move(config)),
    mHwCapabilitiesTopic(BuildVarTopic("hw_capabilities")),
    mEvInfoTopic(BuildVarTopic("ev_info")),
    mPowermeterTopic(BuildVarTopic("powermeter")),
    mLimitsTopic(BuildVarTopic("limits")),
    mSessionEventTopic(BuildVarTopic("session_event"))
{}

EverestMqttThread::~EverestMqttThread()
{
    Stop();
}

void EverestMqttThread::Start()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mThread.joinable())
    {
        return;
    }

    mShouldStop         = false;
    mReconnectRequested = true;
    mConnectionState    = ConnectionState::Disconnected;
    mThread             = std::thread(&EverestMqttThread::ThreadMain, this);
}

void EverestMqttThread::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mThread.joinable())
        {
            mConnectionState = ConnectionState::Stopped;
            return;
        }

        mShouldStop = true;
    }

    mCondition.notify_all();
    mThread.join();
    mConnectionState = ConnectionState::Stopped;
}

void EverestMqttThread::RequestReconnect()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mReconnectRequested = true;
    }

    mCondition.notify_all();
}

EverestMqttThread::ConnectionState EverestMqttThread::GetConnectionState() const
{
    return mConnectionState.load();
}

void EverestMqttThread::HandleConnect(struct mosquitto * mosq, void * obj, int rc)
{
    static_cast<void>(mosq);
    auto * self = static_cast<EverestMqttThread *>(obj);
    VerifyOrReturn(self != nullptr);

    if (rc != MOSQ_ERR_SUCCESS)
    {
        ChipLogError(AppServer, "[%s] MQTT connect callback returned rc=%d", kLogModule, rc);
        return;
    }

    ChipLogProgress(AppServer, "[%s] MQTT session established, subscribing to EVerest EVSE vars", kLogModule);
    if (!self->SubscribeTopics())
    {
        ChipLogError(AppServer, "[%s] Failed to subscribe to one or more EVerest topics", kLogModule);
    }
}

void EverestMqttThread::HandleDisconnect(struct mosquitto * mosq, void * obj, int rc)
{
    static_cast<void>(mosq);
    auto * self = static_cast<EverestMqttThread *>(obj);
    VerifyOrReturn(self != nullptr);

    if (rc == MOSQ_ERR_SUCCESS)
    {
        ChipLogProgress(AppServer, "[%s] MQTT disconnected cleanly", kLogModule);
        return;
    }

    ChipLogError(AppServer, "[%s] MQTT disconnected unexpectedly rc=%d", kLogModule, rc);
    self->RequestReconnect();
}

void EverestMqttThread::HandleMessage(struct mosquitto * mosq, void * obj, const struct mosquitto_message * message)
{
    static_cast<void>(mosq);
    auto * self = static_cast<EverestMqttThread *>(obj);
    VerifyOrReturn(self != nullptr && message != nullptr && message->topic != nullptr);

    const std::string topic(message->topic);
    const std::string payload(message->payload != nullptr ? static_cast<const char *>(message->payload) : "",
                              message->payloadlen > 0 ? static_cast<size_t>(message->payloadlen) : 0);
    self->HandleMessage(topic, payload);
}

void EverestMqttThread::ThreadMain()
{
    mosquitto_lib_init();

    ChipLogProgress(AppServer, "[%s] Worker thread started for mqtt://%s:%u", kLogModule, mConfig.brokerHost.c_str(),
                    static_cast<unsigned>(mConfig.brokerPort));

    std::unique_lock<std::mutex> lock(mMutex);
    while (!mShouldStop)
    {
        if (!mReconnectRequested)
        {
            mCondition.wait(lock, [this] { return mShouldStop || mReconnectRequested; });
            continue;
        }

        mReconnectRequested = false;
        mConnectionState    = ConnectionState::Connecting;

        lock.unlock();
        const bool connected = Connect();
        lock.lock();

        if (mShouldStop)
        {
            break;
        }

        if (!connected)
        {
            mConnectionState = ConnectionState::Disconnected;
            mCondition.wait_for(lock, mConfig.retryBackoff, [this] { return mShouldStop || mReconnectRequested; });
            if (!mShouldStop)
            {
                mReconnectRequested = true;
            }
            continue;
        }

        mConnectionState = ConnectionState::Connected;

        while (!mShouldStop && !mReconnectRequested)
        {
            lock.unlock();
            const int rc = mosquitto_loop(mMosquitto, kLoopTimeoutMs, 1);
            lock.lock();

            if (mShouldStop || mReconnectRequested)
            {
                break;
            }

            if (rc == MOSQ_ERR_SUCCESS)
            {
                continue;
            }

            ChipLogError(AppServer, "[%s] MQTT loop failed with rc=%d, scheduling reconnect", kLogModule, rc);
            mConnectionState    = ConnectionState::Disconnected;
            mReconnectRequested = true;
        }

        lock.unlock();
        Disconnect();
        lock.lock();
    }

    lock.unlock();
    Disconnect();
    mosquitto_lib_cleanup();

    ChipLogProgress(AppServer, "[%s] Worker thread stopping", kLogModule);
}

bool EverestMqttThread::EnsureClient()
{
    if (mMosquitto != nullptr)
    {
        return true;
    }

    mMosquitto = mosquitto_new(mConfig.clientId.c_str(), true, this);
    if (mMosquitto == nullptr)
    {
        ChipLogError(AppServer, "[%s] mosquitto_new failed", kLogModule);
        return false;
    }

    mosquitto_reconnect_delay_set(mMosquitto, kReconnectDelaySeconds, kReconnectDelayMaxSeconds, true);
    mosquitto_connect_callback_set(mMosquitto, &EverestMqttThread::HandleConnect);
    mosquitto_disconnect_callback_set(mMosquitto, &EverestMqttThread::HandleDisconnect);
    mosquitto_message_callback_set(mMosquitto, &EverestMqttThread::HandleMessage);
    return true;
}

bool EverestMqttThread::Connect()
{
    if (!EnsureClient())
    {
        return false;
    }

    const int rc = mosquitto_connect(mMosquitto, mConfig.brokerHost.c_str(), static_cast<int>(mConfig.brokerPort),
                                     kKeepAliveSeconds);
    if (rc != MOSQ_ERR_SUCCESS)
    {
        ChipLogError(AppServer, "[%s] Failed to connect to mqtt://%s:%u rc=%d", kLogModule, mConfig.brokerHost.c_str(),
                     static_cast<unsigned>(mConfig.brokerPort), rc);
        return false;
    }

    ChipLogProgress(AppServer, "[%s] Connected to mqtt://%s:%u", kLogModule, mConfig.brokerHost.c_str(),
                    static_cast<unsigned>(mConfig.brokerPort));
    return true;
}

void EverestMqttThread::Disconnect()
{
    if (mMosquitto == nullptr)
    {
        return;
    }

    const int rc = mosquitto_disconnect(mMosquitto);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN)
    {
        ChipLogError(AppServer, "[%s] mosquitto_disconnect failed rc=%d", kLogModule, rc);
    }

    mosquitto_destroy(mMosquitto);
    mMosquitto = nullptr;
}

bool EverestMqttThread::SubscribeTopics()
{
    if (mMosquitto == nullptr)
    {
        return false;
    }

    const char * const topics[] = { mHwCapabilitiesTopic.c_str(), mEvInfoTopic.c_str(), mPowermeterTopic.c_str(),
                                    mLimitsTopic.c_str(), mSessionEventTopic.c_str() };
    for (const char * topic : topics)
    {
        const int rc = mosquitto_subscribe(mMosquitto, nullptr, topic, 2);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            ChipLogError(AppServer, "[%s] Failed to subscribe to %s rc=%d", kLogModule, topic, rc);
            return false;
        }
        ChipLogProgress(AppServer, "[%s] Subscribed to %s", kLogModule, topic);
    }

    return true;
}

std::string EverestMqttThread::BuildVarTopic(const std::string & varName) const
{
    return mConfig.everestPrefix + "/modules/" + mConfig.evseModuleId + "/impl/" + mConfig.evseImplementationId +
        "/var/" + varName;
}

void EverestMqttThread::HandleMessage(const std::string & topic, const std::string & payload)
{
    switch (ParseTopicSelector(topic))
    {
    case TopicSelector::kHwCapabilities:
        HandleHwCapabilitiesMessage(payload);
        break;
    case TopicSelector::kEvInfo:
        HandleEvInfoMessage(payload);
        break;
    case TopicSelector::kPowermeter:
        HandlePowermeterMessage(payload);
        break;
    case TopicSelector::kLimits:
        HandleLimitsMessage(payload);
        break;
    case TopicSelector::kSessionEvent:
        HandleSessionEventMessage(payload);
        break;
    case TopicSelector::kUnknown:
    default:
        ChipLogError(AppServer, "[%s] Received message on unexpected topic %s", kLogModule, topic.c_str());
        break;
    }
}

void EverestMqttThread::HandleHwCapabilitiesMessage(const std::string & payload)
{
    Json::Value data;
    if (!ParseEVerestVarPayload(payload, data))
    {
        return;
    }

    // EVerest `hw_capabilities.max_current_A_import` and `max_current_A_export` are the installed
    // hardware ceilings in amps. Matter expects the same constraints in milliamps via the delegate.
    const auto maxCurrentMilliAmps = JsonCurrentToMilliAmps(data["max_current_A_import"]);
    if (!maxCurrentMilliAmps.has_value())
    {
        ChipLogError(AppServer, "[%s] hw_capabilities payload is missing max_current_A_import", kLogModule);
        return;
    }

    MatterEvseUpdate update;
    bool hasChanges = false;
    if (mLastHardwareMaxCurrentMilliAmps != maxCurrentMilliAmps)
    {
        mLastHardwareMaxCurrentMilliAmps = maxCurrentMilliAmps;
        update.maxHardwareChargeCurrentLimitMilliAmps = maxCurrentMilliAmps;
        hasChanges = true;
    }

    const auto maxDischargeCurrentMilliAmps = JsonCurrentToMilliAmps(data["max_current_A_export"]);
    if (maxDischargeCurrentMilliAmps.has_value() &&
        mLastHardwareMaxDischargeCurrentMilliAmps != maxDischargeCurrentMilliAmps)
    {
        mLastHardwareMaxDischargeCurrentMilliAmps = maxDischargeCurrentMilliAmps;
        update.maxHardwareDischargeCurrentLimitMilliAmps = maxDischargeCurrentMilliAmps;
        hasChanges = true;
    }

    if (hasChanges)
    {
        ScheduleMatterUpdate(std::move(update));
    }
}

void EverestMqttThread::HandleEvInfoMessage(const std::string & payload)
{
    Json::Value data;
    if (!ParseEVerestVarPayload(payload, data))
    {
        return;
    }

    MatterEvseUpdate update;
    bool hasChanges = false;

    // EVerest `ev_info.soc` is the EV battery state of charge in percent; Matter stores it as a nullable integer percent.
    const auto stateOfChargePercent = JsonSocToPercent(data["soc"]);
    if (stateOfChargePercent.has_value())
    {
        if (mLastStateOfChargePercent != stateOfChargePercent)
        {
            mLastStateOfChargePercent = stateOfChargePercent;
            update.stateOfChargePercent = stateOfChargePercent;
            hasChanges = true;
        }
    }
    else if (mLastStateOfChargePercent.has_value())
    {
        mLastStateOfChargePercent.reset();
        update.clearStateOfCharge = true;
        hasChanges = true;
    }

    // EVerest `ev_info.battery_capacity` is reported in Wh; Matter uses mWh for BatteryCapacity.
    std::optional<int64_t> batteryCapacity;
    if (data["battery_capacity"].isNumeric())
    {
        const double milliWattHours = data["battery_capacity"].asDouble() * 1000.0;
        if (std::isfinite(milliWattHours) &&
            milliWattHours >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
            milliWattHours <= static_cast<double>(std::numeric_limits<int64_t>::max()))
        {
            batteryCapacity = static_cast<int64_t>(std::llround(milliWattHours));
        }
    }

    if (batteryCapacity.has_value())
    {
        if (mLastBatteryCapacityMilliWattHours != batteryCapacity)
        {
            mLastBatteryCapacityMilliWattHours = batteryCapacity;
            update.batteryCapacityMilliWattHours = batteryCapacity;
            hasChanges = true;
        }
    }
    else if (mLastBatteryCapacityMilliWattHours.has_value())
    {
        mLastBatteryCapacityMilliWattHours.reset();
        update.clearBatteryCapacity = true;
        hasChanges = true;
    }

    // EVerest `ev_info.evcc_id` is the cleanest available stable vehicle identifier today, so map it to Matter VehicleID.
    const Json::Value & vehicleIdValue = data["evcc_id"];
    if (vehicleIdValue.isString() && !vehicleIdValue.asString().empty())
    {
        const std::string vehicleId = vehicleIdValue.asString();
        if (mLastVehicleId != vehicleId)
        {
            mLastVehicleId = vehicleId;
            update.vehicleId = vehicleId;
            hasChanges = true;
        }
    }
    else if (mLastVehicleId.has_value())
    {
        mLastVehicleId.reset();
        update.clearVehicleId = true;
        hasChanges = true;
    }

    if (hasChanges)
    {
        ScheduleMatterUpdate(std::move(update));
    }
}

void EverestMqttThread::HandlePowermeterMessage(const std::string & payload)
{
    Json::Value data;
    if (!ParseEVerestVarPayload(payload, data))
    {
        return;
    }

    MatterEvseUpdate update;
    bool hasChanges = false;

    // EVerest `powermeter.voltage_V` is reported in volts; the Matter delegate expects nominal mains voltage in mV.
    const auto nominalMainsVoltageMilliVolts = JsonVoltageToMilliVolts(data["voltage_V"]);
    if (nominalMainsVoltageMilliVolts.has_value() &&
        mLastNominalMainsVoltageMilliVolts != nominalMainsVoltageMilliVolts)
    {
        mLastNominalMainsVoltageMilliVolts = nominalMainsVoltageMilliVolts;
        update.nominalMainsVoltageMilliVolts = nominalMainsVoltageMilliVolts;
        hasChanges = true;
    }

    const auto importMilliWattHours = JsonEnergyToMilliWattHours(data["energy_Wh_import"]);
    const auto exportMilliWattHours = JsonEnergyToMilliWattHours(data["energy_Wh_export"]);
    if (importMilliWattHours.has_value())
    {
        mLastPowermeterImportMilliWattHours = importMilliWattHours;
    }
    if (exportMilliWattHours.has_value())
    {
        mLastPowermeterExportMilliWattHours = exportMilliWattHours;
    }

    // Matter session energy attributes are cumulative deltas in mWh, so derive them from the EVerest powermeter totals.
    if (mCurrentSessionStart.has_value())
    {
        if (importMilliWattHours.has_value() && mSessionEnergyImportStartMilliWattHours.has_value())
        {
            update.sessionEnergyChargedMilliWattHours =
                std::max<int64_t>(0, importMilliWattHours.value() - mSessionEnergyImportStartMilliWattHours.value());
            hasChanges = true;
        }

        if (exportMilliWattHours.has_value() && mSessionEnergyExportStartMilliWattHours.has_value())
        {
            update.sessionEnergyDischargedMilliWattHours =
                std::max<int64_t>(0, exportMilliWattHours.value() - mSessionEnergyExportStartMilliWattHours.value());
            hasChanges = true;
        }

        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - mCurrentSessionStart.value())
                .count();
        update.sessionDurationSeconds = static_cast<uint32_t>(std::max<int64_t>(0, elapsed));
        hasChanges = true;
    }

    if (hasChanges)
    {
        ScheduleMatterUpdate(std::move(update));
    }
}

void EverestMqttThread::HandleLimitsMessage(const std::string & payload)
{
    Json::Value data;
    if (!ParseEVerestVarPayload(payload, data))
    {
        return;
    }

    // EVerest `limits.max_current` is the instantaneous effective current limit being offered to the EV.
    // Until we add a dedicated bridge contract, use it as Matter's CircuitCapacity in milliamps.
    const auto circuitCapacityMilliAmps = JsonCurrentToMilliAmps(data["max_current"]);
    if (!circuitCapacityMilliAmps.has_value())
    {
        ChipLogError(AppServer, "[%s] limits payload is missing max_current", kLogModule);
        return;
    }

    if (mLastCircuitCapacityMilliAmps == circuitCapacityMilliAmps)
    {
        return;
    }

    mLastCircuitCapacityMilliAmps = circuitCapacityMilliAmps;

    // EVerest does not currently expose a dedicated public circuit-capacity variable, so use the effective current
    // limit until a cleaner bridge contract is added.
    MatterEvseUpdate update;
    update.circuitCapacityMilliAmps = circuitCapacityMilliAmps;
    ScheduleMatterUpdate(std::move(update));
}

void EverestMqttThread::HandleSessionEventMessage(const std::string & payload)
{
    Json::Value data;
    if (!ParseEVerestVarPayload(payload, data))
    {
        return;
    }

    const Json::Value & eventValue = data["event"];
    if (!eventValue.isString())
    {
        ChipLogError(AppServer, "[%s] session_event payload is missing event", kLogModule);
        return;
    }

    // EVerest `session_event.event` is currently the cleanest public signal for the Matter State attribute.
    // Session/setup/pause-without-demand events map to PluggedInNoDemand, active transfer / EVSE-paused-demand
    // events map to PluggedInDemand, and SessionFinished maps back to NotPluggedIn.
    MatterEvseUpdate update;
    bool hasChanges = false;

    const Json::Value & uuidValue = data["uuid"];
    const std::string sessionUuid = uuidValue.isString() ? uuidValue.asString() : std::string();

    std::optional<StateEnum> matterEvseState;
    switch (ParseSessionEventSelector(eventValue.asString()))
    {
    case SessionEventSelector::kPluggedInNoDemand:
        matterEvseState = StateEnum::kPluggedInNoDemand;
        break;
    case SessionEventSelector::kPluggedInDemand:
        matterEvseState = StateEnum::kPluggedInDemand;
        break;
    case SessionEventSelector::kUnplugged:
        matterEvseState = StateEnum::kNotPluggedIn;
        break;
    case SessionEventSelector::kUnknown:
    default:
        return;
    }

    if (!matterEvseState.has_value())
    {
        return;
    }

    if (mLastMatterEvseState != static_cast<int>(matterEvseState.value()))
    {
        mLastMatterEvseState = static_cast<int>(matterEvseState.value());
        update.evseState = matterEvseState;
        hasChanges = true;
    }

    if (matterEvseState.value() != StateEnum::kNotPluggedIn)
    {
        bool newSession = false;
        if (!sessionUuid.empty())
        {
            // EVerest exposes a string UUID per session, while Matter expects a uint32 SessionID.
            // Use a stable hash so the same EVerest session always maps to the same Matter ID.
            const uint32_t sessionId = SessionUuidToMatterId(sessionUuid);
            if (mCurrentSessionId != sessionId)
            {
                mCurrentSessionId = sessionId;
                update.sessionId = sessionId;
                hasChanges = true;
                newSession = true;
            }
        }

        if (newSession || !mCurrentSessionStart.has_value())
        {
            mCurrentSessionStart = std::chrono::steady_clock::now();
            if (mLastPowermeterImportMilliWattHours.has_value())
            {
                mSessionEnergyImportStartMilliWattHours = mLastPowermeterImportMilliWattHours;
            }
            if (mLastPowermeterExportMilliWattHours.has_value())
            {
                mSessionEnergyExportStartMilliWattHours = mLastPowermeterExportMilliWattHours;
            }

            const Json::Value & startedMeterValue = data["session_started"]["meter_value"];
            const auto importStart = JsonEnergyToMilliWattHours(startedMeterValue["energy_Wh_import"]);
            const auto exportStart = JsonEnergyToMilliWattHours(startedMeterValue["energy_Wh_export"]);
            if (importStart.has_value())
            {
                mSessionEnergyImportStartMilliWattHours = importStart;
            }
            if (exportStart.has_value())
            {
                mSessionEnergyExportStartMilliWattHours = exportStart;
            }

            update.sessionDurationSeconds = 0;
            update.sessionEnergyChargedMilliWattHours = 0;
            update.sessionEnergyDischargedMilliWattHours = 0;
            hasChanges = true;
        }
    }
    else
    {
        if (mCurrentSessionStart.has_value())
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - mCurrentSessionStart.value())
                                     .count();
            update.sessionDurationSeconds = static_cast<uint32_t>(std::max<int64_t>(0, elapsed));
            hasChanges = true;
        }

        const Json::Value & finishedMeterValue = data["session_finished"]["meter_value"];
        const auto importFinish = JsonEnergyToMilliWattHours(finishedMeterValue["energy_Wh_import"]);
        const auto exportFinish = JsonEnergyToMilliWattHours(finishedMeterValue["energy_Wh_export"]);
        if (importFinish.has_value() && mSessionEnergyImportStartMilliWattHours.has_value())
        {
            update.sessionEnergyChargedMilliWattHours =
                std::max<int64_t>(0, importFinish.value() - mSessionEnergyImportStartMilliWattHours.value());
            hasChanges = true;
        }
        if (exportFinish.has_value() && mSessionEnergyExportStartMilliWattHours.has_value())
        {
            update.sessionEnergyDischargedMilliWattHours =
                std::max<int64_t>(0, exportFinish.value() - mSessionEnergyExportStartMilliWattHours.value());
            hasChanges = true;
        }

        if (mLastStateOfChargePercent.has_value())
        {
            mLastStateOfChargePercent.reset();
            update.clearStateOfCharge = true;
            hasChanges = true;
        }
        if (mLastBatteryCapacityMilliWattHours.has_value())
        {
            mLastBatteryCapacityMilliWattHours.reset();
            update.clearBatteryCapacity = true;
            hasChanges = true;
        }
        if (mLastVehicleId.has_value())
        {
            mLastVehicleId.reset();
            update.clearVehicleId = true;
            hasChanges = true;
        }

        mCurrentSessionStart.reset();
        mSessionEnergyImportStartMilliWattHours.reset();
        mSessionEnergyExportStartMilliWattHours.reset();
    }

    if (hasChanges)
    {
        ScheduleMatterUpdate(std::move(update));
    }
}
