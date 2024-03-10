#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif
using namespace std;

void getNow(timeval *tv);
int64_t getNowMs();

#define TNOW getNow()
#define TNOWMS getNowMs()


class ThreadPool {
  protected:
    struct TaskFunc {
        TaskFunc(uint64_t expireTime) : _expireTime(expireTime) {}
        std::function<void()> _func;
        int64_t _expireTime = 0;
    };

    typedef shared_ptr<TaskFunc> TaskFuncPtr;

  public:
    ThreadPool();

    virtual ~ThreadPool();

    bool Init(size_t num);

    size_t GetThreadNum() {
        std::unique_lock<std::mutex> lock(mutex_);
        return threads_.size();
    }

    size_t GetJobNum() {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    void Stop();

    bool Start();

    template <class F, class... Args>
    auto Exec(F &&f, Args &&...args) -> std::future<decltype(f(args...))> {
        return Exec(0, f, args...);
    }
    //用函数模板方便任务的添加参数 task的构造参数  function  function的 args
    //首先构造任务 再将任务加入到任务队列
    template <class F, class... Args>
    auto Exec(int64_t timeoutMs, F &&f, Args &&...args)
        -> std::future<decltype(f(args...))> {
        int64_t expireTime =
            (timeoutMs == 0 ? 0 : TNOWMS + timeoutMs); 
        using RetType = decltype(f(args...)); 
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        TaskFuncPtr fPtr = std::make_shared<TaskFunc>(
            expireTime);         
        fPtr->_func = [task]() { 
            (*task)();
        };

        std::unique_lock<std::mutex> lock(mutex_);
        tasks_.push(fPtr); 
        condition_.notify_one(); 
        return task->get_future();
    }

    bool WaitForAllDone(int millsecond = -1);

  protected:
    bool Get(TaskFuncPtr &task);

    bool IsTerminate() { return terminate_; }

    void Run();

  protected:
    queue<TaskFuncPtr> tasks_;

    std::vector<std::thread *> threads_; 

    std::mutex mutex_;

    std::condition_variable condition_;

    size_t thread_num_;

    bool terminate_;

    std::atomic<int> atomic_{0};
};



#endif