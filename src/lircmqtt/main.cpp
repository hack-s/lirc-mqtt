//
// Created by michi on 1/13/23.
//

#include "lirc_client.h"
#include "DeviceState.h"

#include <iostream>
#include <string>
#include <thread>
#include "MqttConsumer.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include <csignal>

using namespace std;
using namespace std::chrono;

/////////////////////////////////////////////////////////////////////////////

std::shared_ptr<lm::DeviceStateManager> parseDeviceStates(const std::string& file) {

    rapidjson::Document root;

    FILE* fp = fopen(file.c_str(), "rb"); // non-Windows use "r"

    char readBuffer[65536];
    rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));

    root.ParseStream(is);

    fclose(fp);

    std::string ir_service_name = root["properties"]["irServiceName"].GetString();
    std::string discovery_topic = root["properties"]["discoveryTopic"].GetString();
    std::string mqtt_server = root["properties"]["mqttServer"].GetString();
    std::string device_topic_prefix = root["properties"]["deviceTopicPrefix"].GetString();

    auto deviceStateManager = std::make_shared<lm::DeviceStateManager>(lm::Properties{ir_service_name, discovery_topic, mqtt_server, device_topic_prefix});

    for (const auto& l : root["devices"].GetArray()) {
        deviceStateManager->addDeviceState(l);
    }

    return deviceStateManager;
}

namespace {
    std::function<void(int)> shutdown_handler;
    void signal_handler(int signal) { shutdown_handler(signal); }
}

int main(int argc, char* argv[])
{
    if (argc < 1) {
        cerr << "Expected argument [configfile.json] is missing";
        return 1;
    }

    cout << "Starting lirc-mqtt..."<< std::endl;
    cout << "Loading configuration from " << argv[1] << std::endl;
    auto deviceStateManager = parseDeviceStates(argv[1]);

    auto mqttConsumer = std::make_shared<lm::MqttConsumer>(deviceStateManager);

    // register signal SIGABRT and signal handler
    signal(SIGABRT, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    shutdown_handler = [mqttConsumer] (int signal_num) {
        mqttConsumer->stop();
    };

    return mqttConsumer->consume();
}

/*

 {
   "properties": {
     "ir_service_name": "ir1",
     "discovery_topic": "",
     "mqttServer": ""
   },
   "devices": [
     {
       "device": "m1",
       "buttons": ["", ""]
       "toggles": [
         {
           "name": "brightness",
           "button_up": "BRIGHTNISS_UP",
           "button_down": "BRIGHTNISS_DOWN",
           "type": "range",
           "values": ["1", "4"]
         },
         {
           "name": "led_toggle",
           "button_up": "LED_TOGGLE",
           "button_down": "LED_TOGGLE",
           "type": "switch",
           "values": ["ON", "OFF"]
         },
         {
           "name": "preset",
           "button_up": "LED_TOGGLE",
           "button_down": "LED_TOGGLE",
           "type": "enum",
           "values": ["bla1", "bla2"]
         }
       ]
     }

   ]

 */