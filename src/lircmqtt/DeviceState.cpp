//
// Created by michi on 1/13/23.
//

#include "DeviceState.h"

#include "json/json.h"

#include <utility>

namespace lm {
    void DeviceStateManager::addDeviceState(const Json::Value &json) {

        DeviceState deviceState;
        deviceState._name = json["name"].asString();
        if (json.isMember("buttons")) {
            for (const auto & buttonValue : json["buttons"]) {
                deviceState._buttons.push_back(buttonValue.asString());
            }
        }
        for (Json::ArrayIndex  i=0; i < json["toggles"].size(); i++) {
            DeviceToggle deviceToggle;
            deviceToggle._name = json["toggles"][i]["name"].asString();
            deviceToggle._button_up = json["toggles"][i]["buttonUp"].asString();
            deviceToggle._button_down = json["toggles"][i]["buttonDown"].asString();
            deviceToggle._type = json["toggles"][i]["type"].asString();
            deviceToggle._wrap_around = json["toggles"][i]["wrapAround"].asBool();
            for (const auto & j : json["toggles"][i]["values"]) {
                deviceToggle._values.push_back(j.asString());
            }
            deviceToggle._state = deviceToggle._values[0];
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

        if (toggleIt->second._state == value) {
            rtnNumInvoke = 0;
            return true;
        }

        int currentValue = 0;
        int targetIndex = 0;

        for (int i=0; i < toggleIt->second._values.size(); i++) {
            if (toggleIt->second._state == toggleIt->second._values.at(i)) {
                currentValue = i;
            }

            if (value == toggleIt->second._values.at(i)) {
                targetIndex = i;
            }
        }

        if (targetIndex > currentValue) {
            rtnButton = toggleIt->second._button_up;
            rtnNumInvoke = currentValue - targetIndex;
        } else {
            rtnButton = toggleIt->second._button_down;
            rtnNumInvoke = targetIndex - currentValue;
        }

        if (toggleIt->second._wrap_around && rtnButton.empty()) {
            if (targetIndex > currentValue) {
                rtnButton = toggleIt->second._button_down;
                //rtnNumInvoke = currentValue + (deviceIt->second._values.size() - targetIndex);
            } else {
                rtnButton = toggleIt->second._button_up;
            }
            rtnNumInvoke = toggleIt->second._values.size() - currentValue + targetIndex;
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

    std::shared_ptr<Json::Value> DeviceStateManager::asMqttDescription(const DeviceState& state) {
        std::unique_lock<std::mutex> lock(ml);

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
                feature["value_toggle"] = "TOGGLE";
            }
            if ("enum" == _toggle.second._type) {
                feature["type"] = "enum";

                Json::Value values(Json::arrayValue);

                for (const auto & _value : _toggle.second._values) {
                    values.append(_value);
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

        auto mqttDescription = std::make_shared<Json::Value>();

        (*mqttDescription)["type"] = "device_interview";
        (*mqttDescription)["data"] = data;

        return mqttDescription;
    }

    DeviceStateManager::DeviceStateManager(Properties properties) : _properties(std::move(properties)) {

    }



} // lm