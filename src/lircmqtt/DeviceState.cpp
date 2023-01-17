//
// Created by michi on 1/13/23.
//

#include "DeviceState.h"

#include "json/json.h"

#include <utility>
#include <iostream>

namespace lm {
    void DeviceStateManager::addDeviceState(const Json::Value &json) {

        DeviceState deviceState;
        deviceState._name = json["deviceName"].asString();

        std::cout << "Adding device config for " << deviceState._name << std::endl;

        if (json.isMember("buttons")) {
            for (const auto & buttonValue : json["buttons"]) {
                deviceState._buttons.push_back(buttonValue.asString());
            }
        }
        for (auto deviceToggleJson : json["toggles"]) {
            DeviceToggle deviceToggle;

            deviceToggle._name = deviceToggleJson["name"].asString();
            if (deviceToggleJson.isMember("buttonUp")) {
                deviceToggle._button_up = deviceToggleJson["buttonUp"].asString();
            }
            if (deviceToggleJson.isMember("buttonDown")) {
                deviceToggle._button_down = deviceToggleJson["buttonDown"].asString();
            }
            
            deviceToggle._type = deviceToggleJson["type"].asString();

            if (deviceToggleJson.isMember("wrapAround")) {
                deviceToggle._wrap_around = deviceToggleJson["wrapAround"].asBool();
            } else {
                deviceToggle._wrap_around = false;
            }
            
            if (deviceToggleJson.isMember("values")) {
                for (const auto &j: deviceToggleJson["values"]) {
                    deviceToggle._values.push_back(j.asString());
                }

                if (!deviceToggle._values.empty()) {
                    deviceToggle._state = deviceToggle._values[0];
                }
            }

            if (deviceToggleJson.isMember("valueButtonMappings")) {
                std::string initValue;
                auto valueButtonMappings = deviceToggleJson["valueButtonMappings"];
                for (Json::ArrayIndex j=0; j < valueButtonMappings.size(); j++) {
                    auto value = valueButtonMappings[j]["value"].asString();
                    if (j == 0) {
                        initValue = value;
                    }
                    deviceToggle._valueToButtonMappings.insert(std::make_pair(valueButtonMappings[j]["button"].asString(), value));
                }
                deviceToggle._state = initValue;
            }
            
            deviceState._toggles.insert(std::make_pair(deviceToggle._name, deviceToggle));
        }
        std::unique_lock<std::mutex> lock(ml);
        _deviceStates.insert(std::make_pair(deviceState._name, deviceState));
    }


    bool DeviceStateManager::moveToState(const std::string &deviceName, const std::string& toggleName, const std::string &value, std::string& rtnButton, std::size_t& rtnNumInvoke) {

        std::unique_lock<std::mutex> lock(ml);

        auto deviceIt = _deviceStates.find(deviceName);

        if (deviceIt == _deviceStates.end()) {
            return false;
        }

        auto toggleIt = deviceIt->second._toggles.find(toggleName);

        if (toggleIt == deviceIt->second._toggles.end()) {
            return false;
        }

        if (!toggleIt->second._button_up.empty() || !toggleIt->second._button_down.empty()) {
            return moveToStateUpDown(value, toggleIt->second, rtnButton, rtnNumInvoke);
        } else if (!toggleIt->second._valueToButtonMappings.empty()) {
            return moveToSButtonValueMapping(value, toggleIt->second, rtnButton, rtnNumInvoke);
        } else {
            return false;
        }
    }

    bool DeviceStateManager::moveToSButtonValueMapping(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, size_t &rtnNumInvoke) const {
        auto mapping = toggle._valueToButtonMappings.find(value);
        if (mapping == toggle._valueToButtonMappings.end()) {
            return false;
        }

        rtnButton = mapping->second;
        rtnNumInvoke = 1;
        return true;
    }
    
    bool DeviceStateManager::moveToStateUpDown(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, size_t &rtnNumInvoke) const {
        
        if (toggle._state == value) {
            rtnNumInvoke = 0;
            return true;
        }

        int currentValue = 0;
        int targetIndex = 0;

        for (int i=0; i < toggle._values.size(); i++) {
            if (toggle._state == toggle._values.at(i)) {
                currentValue = i;
            }

            if (value == toggle._values.at(i)) {
                targetIndex = i;
            }
        }

        if (targetIndex > currentValue) {
            rtnButton = toggle._button_up;
            rtnNumInvoke = currentValue - targetIndex;
        } else {
            rtnButton = toggle._button_down;
            rtnNumInvoke = targetIndex - currentValue;
        }

        if (toggle._wrap_around && rtnButton.empty()) {
            if (targetIndex > currentValue) {
                rtnButton = toggle._button_down;
                //rtnNumInvoke = currentValue + (deviceIt->second._values.size() - targetIndex);
            } else {
                rtnButton = toggle._button_up;
            }
            rtnNumInvoke = toggle._values.size() - currentValue + targetIndex;
        }

        return true;
    }

    bool DeviceStateManager::setState(const std::string &deviceName, const std::string &toggleName, const std::string &value) {

        std::unique_lock<std::mutex> lock(ml);

        auto deviceIt = _deviceStates.find(deviceName);

        if (deviceIt == _deviceStates.end()) {
            return false;
        }

        auto toggleIt = deviceIt->second._toggles.find(toggleName);

        if (toggleIt == deviceIt->second._toggles.end()) {
            return false;
        }

        toggleIt->second._state = value;

        return true;
    }

    bool DeviceStateManager::asMqttDescription(const std::string& deviceName, Json::Value& mqttDescription) {
        std::unique_lock<std::mutex> lock(ml);

        auto stateIt = _deviceStates.find(deviceName);

        if (stateIt == _deviceStates.end()) {
            return false;
        }

        auto state = stateIt->second;

        Json::Value definition;
        definition["description"] = "IR interface for " + state._name;
        definition["model"] = state._name;
        definition["supports_ota"] = false;
        definition["vendor"] = "IR";

        definition["options"] = Json::Value(Json::arrayValue);

        Json::Value exposes;
        exposes["type"] = "ir";

                Json::Value feature;
                feature["access"] = 7;

        for (const auto & _button : state._buttons) {
            feature["description"] = "On/off switch " + _button;
            feature["name"] = _button;
            feature["property"] = _button;
            feature["type"] = "binary";
            feature["value_toggle"] = "TOGGLE";
        }

        for (const auto & _toggle : state._toggles) {
            feature["description"] = "On/off state " + _toggle.second._name;
            feature["name"] = _toggle.second._name;
            feature["property"] = _toggle.second._name;
            if ("range" == _toggle.second._type) {
                feature["type"] = "numeric";
                feature["value_min"] = std::stoi(_toggle.second._values[0]);
                feature["value_max"] = std::stoi(_toggle.second._values[1]);
            }
            if ("switch" == _toggle.second._type) {
                feature["type"] = "binary";
                feature["value_off"] = "OFF";
                feature["value_on"] = "ON";
                //feature["value_toggle"] = "TOGGLE";
            }
            if ("enum" == _toggle.second._type) {
                feature["type"] = "enum";

                Json::Value values(Json::arrayValue);

                for (const auto & _value : _toggle.second._values) {
                    values.append(_value);
                }

                for (const auto & _value : _toggle.second._valueToButtonMappings) {
                    values.append(_value.first);
                }

                feature["values"] = values;
            }
        }

        exposes["features"].append(feature);

        definition["exposes"].append(exposes);

        Json::Value data;

        data["friendly_name"] = state._name;
        data["ieee_address"] = "ir/" + _properties.serviceName + "/" + state._name;
        data["status"] = "successful";
        data["supported"] = true;
        data["definition"] = definition;


        (mqttDescription)["type"] = "device_interview";
        (mqttDescription)["data"] = data;

        return true;
    }

    DeviceStateManager::DeviceStateManager(Properties properties) : _properties(std::move(properties)) {

    }



} // lm