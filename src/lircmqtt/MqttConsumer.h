//
// Created by michi on 1/15/23.
//

#ifndef LIRC_MQTT_MQTTCONSUMER_H
#define LIRC_MQTT_MQTTCONSUMER_H

#include <string>
#include <atomic>
#include "rapidjson/document.h"
#include "mqtt/async_client.h"
#include "DeviceState.h"
#include "BlockingQueue.h"

namespace Json {
    class Value;
}

namespace lm {

    const int QOS = 1;
    const int N_RETRY_ATTEMPTS = 5;

// Callbacks for the success or failures of requested actions.
// This could be used to initiate further action, but here we just log the
// results to the console.

    class action_listener : public virtual mqtt::iaction_listener {
        std::string name_;

        void on_failure(const mqtt::token &tok) override;

        void on_success(const mqtt::token &tok) override;

    public:
        explicit action_listener(std::string name) : name_(std::move(name)) {}
    };

/////////////////////////////////////////////////////////////////////////////

/**
 * Local callback & listener class for use with the client connection.
 * This is primarily intended to receive messages, but it will also monitor
 * the connection to the broker. If the connection is lost, it will attempt
 * to restore the connection and re-subscribe to the topic.
 */
    class callback : public virtual mqtt::callback,
                     public virtual mqtt::iaction_listener {
        // Counter for the number of connection retries
        int nretry_;
        // The MQTT client
        mqtt::async_client &cli_;
        // Options to use if we need to reconnect
        mqtt::connect_options &connOpts_;
        // An action listener to display the result of actions.
        action_listener subListener_;

        std::shared_ptr<DeviceStateManager> _deviceStateManager;
        std::map<std::string, std::pair<std::shared_ptr<BlockingQueue<std::string>>, std::shared_ptr<std::thread>>> messageQueue;

        // This deomonstrates manually reconnecting to the broker by calling
        // connect() again. This is a possibility for an application that keeps
        // a copy of it's original connect_options, or if the app wants to
        // reconnect with different options.
        // Another way this can be done manually, if using the same options, is
        // to just call the async_client::reconnect() method.
        void reconnect();

        // Re-connection failure
        void on_failure(const mqtt::token &tok) override;

        // (Re)connection success
        // Either this or connected() can be used for callbacks.
        void on_success(const mqtt::token &tok) override {}

        // (Re)connection success
        void connected(const std::string &cause) override;

        // Callback for when the connection is lost.
        // This will initiate the attempt to manually reconnect.
        void connection_lost(const std::string &cause) override;

        // Callback for when a message arrives.
        void message_arrived(mqtt::const_message_ptr msg) override;

        void delivery_complete(mqtt::delivery_token_ptr token) override {}

    public:
        callback(mqtt::async_client &cli, mqtt::connect_options &connOpts, const std::shared_ptr<DeviceStateManager>& deviceStateManager);
        ~callback() override;
    };

    class MqttConsumer {
    private:
        std::shared_ptr<DeviceStateManager> _deviceStateManager;
        std::mutex m;
        std::condition_variable cv;
        std::atomic<bool> isRunning;

    public:
        explicit MqttConsumer(const std::shared_ptr<DeviceStateManager>& deviceStateManager);

        int consume();

        void stop() {
            isRunning = false;
            cv.notify_all();
        }
    };

}

#endif //LIRC_MQTT_MQTTCONSUMER_H
