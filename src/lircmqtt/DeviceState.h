//
// Created by michi on 1/13/23.
//

#ifndef LIRC_MQTT_DEVICESTATE_H
#define LIRC_MQTT_DEVICESTATE_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

#include "rapidjson/document.h"

namespace lm {

    struct DeviceToggle {
        std::string _name;
        std::string _initialState;
        std::string _state;
        std::string _type;
        std::vector<std::string> _values;
        std::string _button_forward;
        std::string _button_backwards;
        bool _wrap_around;
        std::map<std::string, std::string> _valueToButtonMappings;
        std::vector<std::string> _reset_state_on;
    };

    struct DeviceState {
        std::string _name;
        std::map<std::string, DeviceToggle> _toggles;
        std::vector<std::string> _buttons;
        long _controlIntervalMs;
    };

    struct Properties {
        std::string serviceName;
        std::string discoveryTopic;
        std::string mqttServer;
        std::string deviceTopicPrefix;
        std::string lircdSocketPath;
    };

    class DeviceStateManager {
    private:
        std::mutex ml;
        Properties _properties;
        std::map<std::string, DeviceState> _deviceStates;

        bool moveToStateUpDown(const std::string& value, DeviceToggle &toggleIt, std::string &rtnButton, int &rtnNumInvoke) const;
        bool moveToButtonValueMapping(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, int &rtnNumInvoke) const;

    public:
        explicit DeviceStateManager(Properties properties);

        void addDeviceState(const rapidjson::Value& json);

        bool moveToState(const std::string& deviceName, const std::string& toggleName, const std::string& value, std::string& rtnButton, int& rtnNumInvokes, bool& rtnResetState, long& rtnControlIntervalMs);
        bool setState(const std::string& deviceName, const std::string& toggleName, const std::string& value);
        bool resetDeviceState(const std::string& deviceName);

        bool asMqttDescription(const std::string& deviceName, rapidjson::Document& mqttDescription, rapidjson::Value& root);

        bool asStateDescription(const std::string& deviceName, rapidjson::Document& mqttDescription, rapidjson::Value& root);

        const Properties& getProperties() {
            return _properties;
        }

        std::vector<std::string> getDeviceNames() {
            std::vector<std::string> names;
            for (const auto & deviceState : _deviceStates) {
                names.push_back(deviceState.first);
            }
            return names;
        }


        };

} // lm

#endif //LIRC_MQTT_DEVICESTATE_H
