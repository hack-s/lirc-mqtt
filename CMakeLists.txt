cmake_minimum_required(VERSION 3.15)
project(lirc-mqtt CXX)
SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(RAPIDJSON_HAS_STDSTRING=1)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_modules")

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

#find_package(paho-mqtt-c)
#find_package(paho-mqtt-cpp)
#find_package(jsoncpp)
find_package(lircclient)

include_directories (src ${LIRCCLIENT_INCLUDE_DIR} ${paho-mqtt-c_INCLUDE_DIR} ${paho-mqtt-cpp_INCLUDE_DIR} ${jsoncpp_INCLUDE_DIR})

add_executable(${PROJECT_NAME} src/lircmqtt/main.cpp src/lircmqtt/DeviceState.cpp src/lircmqtt/DeviceState.h src/lircmqtt/MqttConsumer.cpp src/lircmqtt/MqttConsumer.h src/lircmqtt/BlockingQueue.h)

# Use the global target
target_link_libraries(${PROJECT_NAME} ${LIRCCLIENT_LIBRARY} ${CONAN_LIBS})