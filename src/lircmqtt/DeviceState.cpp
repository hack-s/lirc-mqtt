//
// Created by michi on 1/13/23.
//

#include "DeviceState.h"

#include <utility>
#include <iostream>

namespace lm {
    void DeviceStateManager::addDeviceState(const rapidjson::Value &json) {

        DeviceState deviceState;
        deviceState._name = json["deviceName"].GetString();

        std::cout << "Adding device config for " << deviceState._name << std::endl;

        if (json.HasMember("buttons")) {
            for (const auto & buttonValue : json["buttons"].GetArray()) {
                deviceState._buttons.emplace_back(buttonValue.GetString());
            }
        }
        for (const auto& deviceToggleJson : json["toggles"].GetArray()) {
            DeviceToggle deviceToggle;

            deviceToggle._name = deviceToggleJson["name"].GetString();
            if (deviceToggleJson.HasMember("buttonForward")) {
                deviceToggle._button_forward = deviceToggleJson["buttonForward"].GetString();
            }
            if (deviceToggleJson.HasMember("buttonBackwards")) {
                deviceToggle._button_backwards = deviceToggleJson["buttonBackwards"].GetString();
            }
            
            deviceToggle._type = deviceToggleJson["type"].GetString();

            if (deviceToggleJson.HasMember("wrapAround")) {
                deviceToggle._wrap_around = deviceToggleJson["wrapAround"].GetBool();
            } else {
                deviceToggle._wrap_around = false;
            }
            
            if (deviceToggleJson.HasMember("values")) {
                for (const auto &j: deviceToggleJson["values"].GetArray()) {
                    deviceToggle._values.emplace_back(j.GetString());
                }

                if (!deviceToggle._values.empty()) {
                    deviceToggle._state = deviceToggle._values[0];
                }
            }

            if (deviceToggleJson.HasMember("valueButtonMappings")) {
                std::string initValue;
                auto valueButtonMappings = deviceToggleJson["valueButtonMappings"].GetArray();
                for (size_t j=0; j < valueButtonMappings.Size(); j++) {
                    auto value = valueButtonMappings[j]["value"].GetString();
                    if (j == 0) {
                        initValue = value;
                    }
                    deviceToggle._valueToButtonMappings.insert(std::make_pair(valueButtonMappings[j]["button"].GetString(), value));
                }
                deviceToggle._state = initValue;
            }
            
            deviceState._toggles.insert(std::make_pair(deviceToggle._name, deviceToggle));
        }
        std::unique_lock<std::mutex> lock(ml);
        _deviceStates.insert(std::make_pair(deviceState._name, deviceState));
    }


    bool DeviceStateManager::moveToState(const std::string &deviceName, const std::string& toggleName, const std::string &value, std::string& rtnButton, int& rtnNumInvoke) {

        std::unique_lock<std::mutex> lock(ml);

        auto deviceIt = _deviceStates.find(deviceName);

        if (deviceIt == _deviceStates.end()) {
            return false;
        }

        auto toggleIt = deviceIt->second._toggles.find(toggleName);

        if (toggleIt == deviceIt->second._toggles.end()) {
            return false;
        }

        if (!toggleIt->second._valueToButtonMappings.empty()) {
            return moveToSButtonValueMapping(value, toggleIt->second, rtnButton, rtnNumInvoke);
        } else if (!toggleIt->second._button_forward.empty() || !toggleIt->second._button_backwards.empty()) {
            return moveToStateUpDown(value, toggleIt->second, rtnButton, rtnNumInvoke);
        } else {
            return false;
        }
    }

    bool DeviceStateManager::moveToSButtonValueMapping(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, int &rtnNumInvoke) const {
        auto mapping = toggle._valueToButtonMappings.find(value);
        if (mapping == toggle._valueToButtonMappings.end()) {
            return false;
        }

        rtnButton = mapping->second;
        rtnNumInvoke = 1;
        return true;
    }
    
    bool DeviceStateManager::moveToStateUpDown(const std::string& value, DeviceToggle &toggle, std::string &rtnButton, int &rtnNumInvoke) const {
        
        if (toggle._state == value) {
            rtnNumInvoke = 0;
            return true;
        }

        int currentValue = 0;
        int targetIndex = 0;
        bool isAssigned = false;

        for (int i=0; i < toggle._values.size(); i++) {
            if (toggle._state == toggle._values.at(i)) {
                currentValue = i;
            }

            if (value == toggle._values.at(i)) {
                targetIndex = i;
            }
        }

        if (targetIndex > currentValue) {
            rtnButton = toggle._button_forward;
            rtnNumInvoke = targetIndex - currentValue;
            isAssigned = !rtnButton.empty();
        } else {
            rtnButton = toggle._button_backwards;
            rtnNumInvoke = currentValue - targetIndex;
            isAssigned = !rtnButton.empty();
        }

        if (toggle._wrap_around && rtnButton.empty()) {
            if (targetIndex > currentValue) {
                rtnButton = toggle._button_backwards;
                //rtnNumInvoke = currentValue + (deviceIt->second._values.size() - targetIndex);
            } else {
                rtnButton = toggle._button_forward;
            }
            rtnNumInvoke = toggle._values.size() - currentValue + targetIndex;
            isAssigned = true;
        }

        return isAssigned;
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

    bool DeviceStateManager::asMqttDescription(const std::string& deviceName, rapidjson::Document& mqttDescription) {
        std::unique_lock<std::mutex> lock(ml);

        auto stateIt = _deviceStates.find(deviceName);

        if (stateIt == _deviceStates.end()) {
            return false;
        }

        auto state = stateIt->second;

        auto& allocator = mqttDescription.GetAllocator();

        rapidjson::Value features(rapidjson::kArrayType);
        for (const auto & _button : state._buttons) {
            rapidjson::Value feature(rapidjson::kObjectType);
            feature.AddMember("access", 7, allocator);
            feature.AddMember("description", "On/off switch " + _button, allocator);
            feature.AddMember("name", _button, allocator);
            feature.AddMember("property", _button, allocator);
            feature.AddMember("type", "binary", allocator);
            feature.AddMember("value_toggle", "TOGGLE", allocator);
            features.GetArray().PushBack(feature, allocator);
        }

        for (const auto & _toggle : state._toggles) {
            rapidjson::Value feature(rapidjson::kObjectType);
            feature.AddMember("access", 7, allocator);
            feature.AddMember("description", "On/off state " + _toggle.second._name, allocator);
            feature.AddMember("name", _toggle.second._name, allocator);
            feature.AddMember("property", _toggle.second._name, allocator);
            if ("range" == _toggle.second._type) {
                feature.AddMember("type", "numeric", allocator);
                feature.AddMember("value_min", std::stoi(_toggle.second._values[0]), allocator);
                feature.AddMember("value_max", std::stoi(_toggle.second._values[1]), allocator);
            }
            if ("switch" == _toggle.second._type) {
                feature.AddMember("type", "binary", allocator);
                feature.AddMember("value_off", "OFF", allocator);
                feature.AddMember("value_on", "ON", allocator);
                //feature["value_toggle"] = "TOGGLE";
            }
            if ("enum" == _toggle.second._type) {
                feature.AddMember("type", "enum", allocator);

                rapidjson::Value values(rapidjson::kArrayType);

                for (const auto & _value : _toggle.second._values) {
                    values.GetArray().PushBack(rapidjson::Value(_value, allocator), allocator);
                }

                for (const auto & _value : _toggle.second._valueToButtonMappings) {
                    values.GetArray().PushBack(rapidjson::Value(_value.first, allocator), allocator);
                }

                feature.AddMember("values", values, allocator);
            }
            features.GetArray().PushBack(feature, allocator);
        }

        rapidjson::Value exposedFeature(rapidjson::kObjectType);
        exposedFeature.AddMember("type", "ir", allocator);
        exposedFeature.AddMember("features", features, allocator);

        rapidjson::Value definition(rapidjson::kObjectType);
        definition.AddMember("description", "IR interface for " + state._name, allocator);
        definition.AddMember("model", state._name, allocator);
        definition.AddMember("supports_ota", false, allocator);
        definition.AddMember("vendor", "IR", allocator);

        definition.AddMember("options", rapidjson::Value(rapidjson::kArrayType), allocator);

        rapidjson::Value exposes(rapidjson::kArrayType);
        exposes.GetArray().PushBack(exposedFeature, allocator);
        definition.AddMember("exposes", exposes, allocator);

        mqttDescription.AddMember("friendly_name", state._name, allocator);
        mqttDescription.AddMember("ieee_address", "ir/" + _properties.serviceName + "/" + state._name, allocator);
        mqttDescription.AddMember("status", "successful", allocator);
        mqttDescription.AddMember("supported", true, allocator);
        mqttDescription.AddMember("definition",  definition, allocator);
        mqttDescription.AddMember("type", "EndDevice", allocator);

        return true;
    }

    DeviceStateManager::DeviceStateManager(Properties properties) : _properties(std::move(properties)) {

    }



} // lm