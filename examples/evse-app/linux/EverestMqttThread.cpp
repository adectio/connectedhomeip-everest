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

#include <lib/support/logging/CHIPLogging.h>
#include <mosquitto.h>

namespace {
constexpr const char * kLogModule          = "EverestMQTT";
constexpr int kKeepAliveSeconds            = 60;
constexpr int kLoopTimeoutMs               = 1000;
constexpr int kReconnectDelaySeconds       = 1;
constexpr int kReconnectDelayMaxSeconds    = 5;
} // namespace

EverestMqttThread::EverestMqttThread(Config config) : mConfig(std::move(config)) {}

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
    return true;
}

bool EverestMqttThread::Connect()
{
    if (!EnsureClient())
    {
        return false;
    }

    const int rc = mosquitto_connect(mMosquitto, mConfig.brokerHost.c_str(), static_cast<int>(mConfig.brokerPort), kKeepAliveSeconds);
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
        ChipLogError(AppServer, "[%s] Failed to disconnect cleanly rc=%d", kLogModule, rc);
    }

    mosquitto_destroy(mMosquitto);
    mMosquitto = nullptr;
}
