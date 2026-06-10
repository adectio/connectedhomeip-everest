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
#include <string>
#include <thread>

struct mosquitto;

class EverestMqttThread
{
public:
    struct Config
    {
        std::string brokerHost         = "127.0.0.1";
        uint16_t brokerPort            = 1883;
        std::string clientId           = "matter-evse-linux";
        std::string topicRoot          = "everest/matter/v1/evse/1";
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
    void ThreadMain();
    bool Connect();
    void Disconnect();
    bool EnsureClient();

    Config mConfig;
    std::thread mThread;
    mutable std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<ConnectionState> mConnectionState = ConnectionState::Stopped;
    bool mShouldStop                             = false;
    bool mReconnectRequested                     = false;
    struct mosquitto * mMosquitto                = nullptr;
};
