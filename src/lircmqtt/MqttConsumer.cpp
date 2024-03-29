//
// Created by michi on 1/15/23.
//

#include "MqttConsumer.h"

#include <utility>

#include <thread>
#include <chrono>
#include <lirc_client.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"

void sendLircControl(const std::string& lircdSocketPath, const std::string& deviceName, const std::string& button)
{
    int fd;

    fd = lirc_get_local_socket(lircdSocketPath.c_str(), 0);
    if (fd < 0) {
        std::cout << "Error initializing Lirc" << std::endl;
        return;
    }
    if (lirc_send_one(fd, deviceName.c_str(), button.c_str()) == -1) {
        std::cout << "Error sending Lirc control" << std::endl;
    } else {
        std::cout << "Lirc control was sent successfully" << std::endl;
    }
    close(fd);
}


int lm::MqttConsumer::consume() {
    // A subscriber often wants the server to remember its messages when its
    // disconnected. In that case, it needs a unique ClientID and a
    // non-clean session.

    mqtt::async_client cli(_deviceStateManager->getProperties().mqttServer, _deviceStateManager->getProperties().serviceName);

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(false);

    // Install the callback(s) before connecting.
    callback cb(cli, connOpts, _deviceStateManager);
    cli.set_callback(cb);

    // Start the connection.
    // When completed, the callback will subscribe to topic.

    try {
        std::cout << "Connecting to the MQTT server at " << _deviceStateManager->getProperties().mqttServer << " with client id " << _deviceStateManager->getProperties().serviceName << " ..." << std::endl << std::flush;
        cli.connect(connOpts, nullptr, cb);
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "\nERROR: Unable to connect to MQTT server: '"
                  << _deviceStateManager->getProperties().mqttServer << "'" << exc << std::endl;
        return 1;
    }

    if(!isRunning) {
        return 0;
    }

    // Just block till user tells us to quit.
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk);
    }

    // Disconnect

    try {
        std::cout << "\nDisconnecting from the MQTT server..." << std::flush;
        cli.disconnect()->wait();
        std::cout << "OK" << std::endl;
    }
    catch (const mqtt::exception& exc) {
        std::cerr << exc << std::endl;
        return 1;
    }

    return 0;
}

lm::MqttConsumer::MqttConsumer(const std::shared_ptr<DeviceStateManager>& deviceStateManager) :
    _deviceStateManager(deviceStateManager), isRunning(true) {}

void lm::callback::reconnect() {
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    try {
        cli_.connect(connOpts_, nullptr, *this);
    }
    catch (const mqtt::exception &exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        exit(1);
    }
}

void lm::callback::on_failure(const mqtt::token &tok) {
    std::cout << "Connection attempt failed to " << tok.get_connect_response().get_server_uri() << ", found session: " << tok.get_connect_response().is_session_present() << std::endl;
    if (++nretry_ > N_RETRY_ATTEMPTS)
        exit(1);
    reconnect();
}

void lm::callback::connected(const std::string &cause) {
    std::cout << "\nConnection success" << std::endl;

    std::vector<std::string> allDeviceNames = _deviceStateManager->getDeviceNames();

    for (const auto &deviceName: allDeviceNames) {
        subscribeDeviceUpdates(deviceName);
    }

    sendDeviceDiscovery(allDeviceNames);

    for (const auto &deviceName: allDeviceNames) {
        sendDeviceState(deviceName);
    }
}

void lm::callback::connection_lost(const std::string &cause) {
    std::cout << "\nConnection lost" << std::endl;
    if (!cause.empty())
        std::cout << "\tcause: " << cause << std::endl;

    std::cout << "Reconnecting..." << std::endl;
    nretry_ = 0;
    reconnect();
}

void lm::callback::message_arrived(mqtt::const_message_ptr msg) {
    std::cout << "Message arrived" << std::endl;
    std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
    std::cout << "\tpayload: '" << msg->to_string() << "'\n" << std::endl;

    std::string deviceName = msg->get_topic().substr(_deviceStateManager->getProperties().deviceTopicPrefix.length());
    auto lastSlash = deviceName.find_last_of('/');
    if (lastSlash != std::string::npos) {
        deviceName = deviceName.substr(0, lastSlash);
    }

    auto queueIt = messageQueue.find(deviceName);
    if (queueIt == messageQueue.end()) {
        std::cout << "Error processing message, unknown device: " << deviceName << std::endl;
    } else {
        queueIt->second.first->push(msg->to_string());
    }
}

void do_send_device_state(mqtt::async_client& cli, const std::shared_ptr<lm::DeviceStateManager>& deviceStateManager, const std::string &deviceName) {

    rapidjson::Document mqttDeviceState;
    mqttDeviceState.SetObject();
    deviceStateManager->asStateDescription(deviceName, mqttDeviceState, mqttDeviceState);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    mqttDeviceState.Accept(writer);
    const char *output = buffer.GetString();

    std::cout << "Sending device state update message for " << deviceName << std::endl;
    cli.publish(deviceStateManager->getProperties().deviceTopicPrefix + deviceName, output, 1, false);
}


lm::callback::callback(mqtt::async_client &cli, mqtt::connect_options &connOpts, const std::shared_ptr<DeviceStateManager>& deviceStateManager)
        : nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription"), _deviceStateManager(deviceStateManager) {
    auto names = _deviceStateManager->getDeviceNames();

    for (const auto& deviceName : names) {
        auto queue = std::make_shared<BlockingQueue<std::string>>();
        auto lDeviceStateManager = _deviceStateManager;

        auto t = std::make_shared<std::thread>([lDeviceStateManager, deviceName, queue, this] {

            std::string message;

            std::chrono::milliseconds lastSentTime = std::chrono::milliseconds::min();

            while (queue->pop(message)) {
                rapidjson::Document messageJson;
                messageJson.Parse(message);

                bool wasUpdated = false;
                for (auto it = messageJson.MemberBegin(); it != messageJson.MemberEnd(); ++it) {

                    std::string toggleName = it->name.GetString();
                    std::string value = it->value.GetString();

                    if (toggleName == "reset") {
                        if (value == "TOGGLE") {
                            std::cout << "Resetting state for device " << deviceName << std::endl;
                            lDeviceStateManager->resetDeviceState(deviceName);
                            continue;
                        }
                    }

                    std::vector<std::string> buttons;
                    int numInvokes;
                    bool resetState = false;
                    long controlIntervalMs = 0;

                    if (lDeviceStateManager->moveToState(deviceName, toggleName, value, buttons, numInvokes, resetState, controlIntervalMs)) {
                        std::string buttonString;
                        for (const auto& button : buttons) {
                            buttonString += button + " ";
                        }
                        std::cout << "Invoking IR control for " << deviceName << " with button(s) " << buttonString << ": " << numInvokes << " times" << std::endl;
                        for (int i=0; i < numInvokes; i++) {
                            for (const auto& button : buttons) {
                                if (toggleName == "sleep") {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(value)));
                                } else {
                                    if (controlIntervalMs > 0) {
                                        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch()
                                        );
                                        std::chrono::milliseconds ms = std::chrono::milliseconds(controlIntervalMs);
                                        std::chrono::milliseconds nextSentTime = lastSentTime + ms;
                                        if (nextSentTime > now) {
                                            std::this_thread::sleep_for(nextSentTime - now);
                                        }
                                    }
                                    sendLircControl(lDeviceStateManager->getProperties().lircdSocketPath, deviceName,
                                                    button);
                                    lastSentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()
                                    );
                                }
                            }
                        }
                        wasUpdated = resetState || numInvokes > 0;
                        if (resetState) {
                            lDeviceStateManager->resetDeviceState(deviceName);
                        }
                        lDeviceStateManager->setState(deviceName, toggleName, value);
                    } else {
                        std::cout << "WARN could not determine requires buttons to press to enter state for device: " << deviceName << ", toggle: " << toggleName << ", value: " << std::endl;
                    }
                }
                if (wasUpdated) {
                    do_send_device_state(cli_, lDeviceStateManager, deviceName);
                }
            }
        });

        messageQueue.insert(std::make_pair(deviceName, std::make_pair(queue, t)));

    }
}

lm::callback::~callback() {

    for (auto& queueEntry : messageQueue) {
        queueEntry.second.first->requestShutdown();
    }
    for (auto& queueEntry : messageQueue) {
        queueEntry.second.second->join();
    }
}

void lm::action_listener::on_failure(const mqtt::token &tok) {
    std::cout << name_ << " failure";
    if (tok.get_message_id() != 0)
        std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
    std::cout << std::endl;
}

void lm::action_listener::on_success(const mqtt::token &tok) {
    std::cout << name_ << " success";
    if (tok.get_message_id() != 0)
        std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
    auto top = tok.get_topics();
    if (top && !top->empty())
        std::cout << "\ttoken topic: '" << (*top)[0] << "', ..." << std::endl;
    std::cout << std::endl;
}


void lm::callback::subscribeDeviceUpdates(const std::string& deviceName) {

    std::string deviceTopicName = _deviceStateManager->getProperties().deviceTopicPrefix + deviceName + "/set";

    std::cout << "\nSubscribing to topic '" << deviceTopicName << "'\n"
              << "\tfor client " << _deviceStateManager->getProperties().serviceName
              << " using QoS" << QOS << std::endl;

    cli_.subscribe(deviceTopicName, QOS, nullptr, subListener_);
}

void lm::callback::sendDeviceDiscovery(const std::vector<std::string> &allDeviceNames) {
    rapidjson::Document mqttDeviceInterviews;
    mqttDeviceInterviews.SetArray();

    for (const auto& deviceName : allDeviceNames) {
        std::cout << "Generating device discovery for IR device config: " << deviceName << std::endl;

        rapidjson::Value mqttDeviceInterview(rapidjson::kObjectType);
        if (_deviceStateManager->asMqttDescription(deviceName, mqttDeviceInterviews, mqttDeviceInterview)) {

            mqttDeviceInterviews.GetArray().PushBack(mqttDeviceInterview, mqttDeviceInterviews.GetAllocator());

        } else {
            std::cerr << "Device config not found for IR device config: " << deviceName << std::endl;
        }
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    mqttDeviceInterviews.Accept(writer);
    const char* output = buffer.GetString();

    std::cout << "Sending device discovery message" << std::endl;
    cli_.publish(_deviceStateManager->getProperties().discoveryTopic, output, QOS, true);
}

void lm::callback::sendDeviceState(const std::string &deviceName) {
    do_send_device_state(cli_, _deviceStateManager, deviceName);
}
