#ifndef DG_BLOCK_QUEUE_H
#define DG_BLOCK_QUEUE_H

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace vega {

    template <typename T>
    class BlockQueue
    {
    public:

        T pop()
        {
          std::unique_lock<std::mutex> mlock(mutex_);
          while (queue_.empty())
          {
            cond_.wait(mlock);
          }
          auto val = queue_.front();
          queue_.pop();
          return val;
        }

        void pop(T& item)
        {
          std::unique_lock<std::mutex> mlock(mutex_);
          while (queue_.empty())
          {
            cond_.wait(mlock);
          }
          item = queue_.front();
          queue_.pop();
        }

        void push(const T& item)
        {
          std::unique_lock<std::mutex> mlock(mutex_);
          queue_.push(item);
          mlock.unlock();
          cond_.notify_one();
        }
        bool empty() {
          return queue_.empty();
        }
        inline size_t size() { return queue_.size(); }
        BlockQueue() = default;
        BlockQueue(const BlockQueue&) = delete;            // disable copying
        BlockQueue& operator=(const BlockQueue&) = delete; // disable assignment

    private:
        std::queue<T> queue_;
        std::mutex mutex_;
        std::condition_variable cond_;
    };
}

#endif // DG_BLOCK_QUEUE_H
