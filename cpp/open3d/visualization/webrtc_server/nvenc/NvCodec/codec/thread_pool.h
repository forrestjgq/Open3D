//
// Created by JiangGuoqing on 2019-06-01.
//


#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <map>
#include "open3d/visualization/webrtc_server/nvenc/NvCodec/codec/block_queue.h"

#ifndef VEGA_THREADPOOL_H
#define VEGA_THREADPOOL_H
namespace vega {

    class Doable {
    public:
        Doable() = default;
        virtual ~Doable() = default;

        virtual void start() = 0;

		void setPriorityLevel(int priorityLevel) {
			priority_level_=priorityLevel;
		}
		int priorityLevel() {
			return priority_level_;
		}
	private:
	    int priority_level_=0;
    };

    using DoableSP = std::shared_ptr<Doable>;

    /**
     * Usage:
     * auto cb = std::make_shared<CallbackDoable>();
     * cb->setCallback([=]() {
     *     // here you put codes to execute while this doable is scheduled
     * });
     * station->push(cb); // push callback doable to station
     */
    class CallbackDoable : public Doable {
    public:
        CallbackDoable() = default;
        ~CallbackDoable() override = default;

        using Callback = std::function<void ()>;
        void setCallback(Callback &callback) {
            callback_ = callback;
        }

        void start() override {
            if(callback_) callback_();
        }

    protected:

        Callback callback_;
    };


    class DoableStation {
    private:
        class EndDoable : public Doable {
        public:
            void start() override {
                // do nothing
            }
        };
    public:
        DoableStation(const std::string &name) {
            name_ = name;
            end_ = false;
            thread_ = std::make_shared<std::thread>(std::bind(&DoableStation::work, this));
        }
        virtual ~DoableStation() {
            if(end_)
                return;

            end_ = true;
            put(std::make_shared<EndDoable>());
            thread_->join();
        }
    public:
        /**
         * Set logging while queue size exceeds #moreThan, and print queue state
         * once for every #cnt station put
         */
        inline void setLogging(int cnt, int moreThan) { log_count_ = cnt; top_count_ = moreThan; }

        void put(DoableSP doable) {
            msgq_.push(doable);
        }
        void doAsync(CallbackDoable::Callback &callback) {
            auto doable = std::make_shared<CallbackDoable>();
            doable->setCallback(callback);
            put(doable);
        }
        inline size_t size() { return msgq_.size(); }

    protected:
        /**
         * Main thread processing
         */
        void work() {
            while(!end_) {
                auto doable = msgq_.pop();
                doable->start();
            }
        }


    private:

        BlockQueue<DoableSP> msgq_;    ///<! Blocking message queue
        std::shared_ptr<std::thread> thread_;       ///<! Workstation thread
        bool end_ = true;           ///<! Is thread ended or in ending
        std::string name_;          ///<! Station name
        int log_count_ = 0;
        int top_count_ = 0;
    };
}
#endif //VEGA_THREADPOOL_H
