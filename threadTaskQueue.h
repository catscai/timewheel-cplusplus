
#ifndef _THREAD_TASK_QUEUE_H_
#define _THREAD_TASK_QUEUE_H_
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <map>
#include <atomic>
#include <list>

class ThreadTaskQueue
{
public:
    explicit ThreadTaskQueue(const int& id);
    ~ThreadTaskQueue();
public:
    void Start();
    void AddTask(int64_t nowUnix, int64_t timeout, std::function<void()> f);
    void Stop();
    size_t Size();
    int ID();
    int64_t DropSize(); 
private:
    static void threadaProc(void* pvoid);
private:
    std::list<std::pair<std::function<void()>,int64_t> >   m_taskq;
    std::mutex                          m_lock;
    std::condition_variable             m_cond;
    std::thread*                        m_thread;
    bool                                m_stop;
    std::atomic<int>                    m_timeout;  // 丢包超时
    std::atomic<int64_t>                m_drop;  // 丢包数
    int                                 m_id;   // 编号
};

#endif