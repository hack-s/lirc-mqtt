//
// Created by michi on 1/16/23.
//

#ifndef LIRC_MQTT_BLOCKINGQUEUE_H
#define LIRC_MQTT_BLOCKINGQUEUE_H

#include <condition_variable>
#include <queue>

namespace lm {
    template <typename T> class BlockingQueue {
        std::condition_variable _cvCanPop;
        std::mutex _sync;
        std::queue<T> _qu;
        bool _bShutdown = false;

    public:
        void push(const T& item)
        {
            {
                std::unique_lock<std::mutex> lock(_sync);
                _qu.push(item);
            }
            _cvCanPop.notify_one();
        }

        void requestShutdown() {
            {
                std::unique_lock<std::mutex> lock(_sync);
                _bShutdown = true;
            }
            _cvCanPop.notify_all();
        }

        bool pop(T &item) {
            std::unique_lock<std::mutex> lock(_sync);
            for (;;) {
                if (_qu.empty()) {
                    if (_bShutdown) {
                        return false;
                    }
                }
                else {
                    break;
                }
                _cvCanPop.wait(lock);
            }
            item = std::move(_qu.front());
            _qu.pop();
            return true;
        }
    };
}

#endif //LIRC_MQTT_BLOCKINGQUEUE_H
