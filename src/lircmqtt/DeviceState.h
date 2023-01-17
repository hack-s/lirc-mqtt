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

namespace Json {
    class Value;
}

namespace lm {

    struct DeviceToggle {
        std::string _name;
        std::string _state;
        std::string _type;
        std::vector<std::string> _values;
        std::string _button_up;
        std::string _button_down;
        bool _wrap_around;
        std::map<std::string, std::string> _valueToButtonMappings;
    };

    struct DeviceState {
        std::string _name;
        std::map<std::string, DeviceToggle> _toggles;
        std::vector<std::string> _buttons;
    };

    struct Properties {
        std::string serviceName;
        std::string discoveryTopic;
        std::string mqttServer;
    };

    class DeviceStateManager {
    private:
        std::mutex ml;
        Properties _properties;
        std::map<std::string, DeviceState> _deviceStates;

        bool moveToStateUpDown(const std::string& value, DeviceToggle &toggleIt, std::string &rtnButton, size_t &rtnNumInvoke) const;
        bool moveToSButtonValueMapping(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, size_t &rtnNumInvoke) const;

    public:
        explicit DeviceStateManager(Properties properties);

        void addDeviceState(const Json::Value& json);

        bool moveToState(const std::string& deviceName, const std::string& toggleName, const std::string& value, std::string& rtnButton, std::size_t& rtnNumInvokes);
        bool setState(const std::string& deviceName, const std::string& toggleName, const std::string& value);

        bool asMqttDescription(const std::string& deviceName, Json::Value& mqttDescription);

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
