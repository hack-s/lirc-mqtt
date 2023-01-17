//
// Created by michi on 1/15/23.
//

#include "MqttConsumer.h"

#include <utility>
#include <json/json.h>

#include <thread>
#include <chrono>
#include <lirc_client.h>

void sendLircControl(const std::string& deviceName, const std::string& button)
{
    int fd;

    fd = lirc_get_local_socket(NULL, 0);
    if (fd < 0) {
        std::cout << "Error initializing Lirc" << std::endl;
        return;
    }
    if (lirc_send_one(fd, deviceName.c_str(), button.c_str()) == -1) {
        std::cout << "Error sending Lirc control" << std::endl;
    };
}


int lm::MqttConsumer::consume() {
    // A subscriber often wants the server to remember its messages when its
    // disconnected. In that case, it needs a unique ClientID and a
    // non-clean session.

    mqtt::async_client cli(_deviceStateManager->getProperties().mqttServer, _deviceStateManager->getProperties().serviceName);

    mqtt::connect_options connOpts;
    connOpts.set_clean_session(false);

    // Install the callback(s) before connecting.
    callback cb(cli, connOpts, _deviceStateManager->getProperties());
    cli.set_callback(cb);

    // Start the connection.
    // When completed, the callback will subscribe to topic.

    try {
        std::cout << "Connecting to the MQTT server..." << std::flush;
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
    std::cout << "Connection attempt failed" << std::endl;
    if (++nretry_ > N_RETRY_ATTEMPTS)
        exit(1);
    reconnect();
}

void lm::callback::connected(const std::string &cause) {
    std::cout << "\nConnection success" << std::endl;
    std::cout << "\nSubscribing to topic '" << "ir/#" << "'\n"
              << "\tfor client " << _properties.serviceName
              << " using QoS" << QOS << "\n"
              << "\nPress Q<Enter> to quit\n" << std::endl;

    std::vector<std::string> allDeviceNames = deviceStateManager->getDeviceNames();
    for (const auto& deviceName : allDeviceNames) {
        Json::Value mqttDeviceInterview;
        if (deviceStateManager->asMqttDescription(deviceName, mqttDeviceInterview)) {
            cli_.publish(_properties.discoveryTopic, mqttDeviceInterview.asString());
        }
    }

    cli_.subscribe("ir/#", QOS, nullptr, subListener_);
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

    Json::Value messageJson;
    Json::Reader r;
    r.parse(msg->to_string(), messageJson);

    auto queueIt = messageQueue.find(messageJson["deviceName"].asString());
    if (queueIt == messageQueue.end()) {
        std::cout << "Error processing message, unknown device: " << messageJson["deviceName"] << std::endl;
    } else {
        queueIt->second.first->push(messageJson);
    }
}

lm::callback::callback(mqtt::async_client &cli, mqtt::connect_options &connOpts, const lm::Properties &properties)
        : nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription"), _properties(properties), deviceStateManager(std::make_shared<DeviceStateManager>(properties)) {
    auto names = deviceStateManager->getDeviceNames();

    for (const auto& deviceName : names) {
        auto queue = std::make_shared<BlockingQueue<Json::Value>>();
        auto lDeviceStateManager = deviceStateManager;

        auto t = std::make_shared<std::thread>([lDeviceStateManager, deviceName, queue] {

            Json::Value messageJson;

            while (queue->pop(messageJson)) {
                for (const auto & toggle : messageJson["toggles"]) {

                    std::string toggleName = toggle["deviceName"].asString();
                    std::string value = toggle["value"].asString();

                    std::string button;
                    std::size_t  numInvokes;
                    if (lDeviceStateManager->moveToState(deviceName, toggleName, value, button, numInvokes)) {
                        for (unsigned int i=0; i < numInvokes; i++) {
                            if (toggleName == "sleep") {
                                std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(value)));
                            } else {
                                sendLircControl(deviceName, button);
                            }
                        }
                        lDeviceStateManager->setState(deviceName, toggleName, value);
                    }
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
