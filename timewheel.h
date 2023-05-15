
#ifndef _TIME_WHEEL_H_
#define _TIME_WHEEL_H_
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <cmath>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "threadTaskQueue.h"
#include <iostream>
using namespace std;

/*
    linux下 利用select socketpair 实现时间轮定时器
*/

class TimeWheel {
public:
    struct task;
    typedef std::shared_ptr<task> TaskPtr;
    struct task {
        const int64_t TaskID;     // 任务ID
    private:
        int     TimeOut;    // 超时时间ms
        bool    Circle;     // 是否循环
        bool    Deleted;    // 是否被删除
        TaskPtr Next;
        std::function<void()> F;
    public:
        task(const int64_t& taskID, const int& timeOut, bool circle, std::function<void()> f):TaskID(taskID),TimeOut(timeOut),Circle(circle),Deleted(false),Next(nullptr),F(f){}
        ~task(){
            //cout << "taskID = " << TaskID << " exit" << endl;
        }
        friend class TimeWheel;
    };
    typedef std::shared_ptr<ThreadTaskQueue> ThreadQueuePtr;
public:
    // tickCount: tick数量 timeOfOnceTickMs：每个tick所需时间 numberOfDealThread：异步执行任务的线程数,如果数量为0,那么任务将排队阻塞执行
    explicit TimeWheel(const int& tickCount = 1000, const int& timeOfOnceTickMs = 1000, const int& numberOfDealThread = 2)
        :m_tick_count(tickCount),m_time_once(timeOfOnceTickMs),m_curr_tick(0),m_is_stop(false),m_round_robin(0),m_ticker(nullptr) {
        m_tasks.resize(m_tick_count, nullptr);
        m_threads_queue.resize(numberOfDealThread, nullptr);
        m_task_id.store(0);
    }
    ~TimeWheel() {
        Stop();
    }
public:
    int     Start() {
        int ret = socketpair(AF_LOCAL, SOCK_DGRAM, 0, m_pair); 
        if(ret != 0) {
            return ret;
        }
        for(int i = 0; i < m_threads_queue.size(); i++) {
            m_threads_queue[i] = std::make_shared<ThreadTaskQueue>(i);
            m_threads_queue[i]->Start();
        }
        m_ticker = new std::thread(&TimeWheel::run, this);
        return 0;
    }

    int    Stop() {
        if(m_is_stop.load()) {
            return 0;
        }
        m_is_stop.store(true);
        int ret = send(m_pair[0], "1", 1, 0);
        if(ret != 0) {
            return ret;
        }
        for(int i = 0; i < m_threads_queue.size(); i++) {
            m_threads_queue[i]->Stop();
        }
        m_ticker->join();
        delete m_ticker;
        close(m_pair[0]);
        close(m_pair[1]);
        return 0;
    }

    TaskPtr   AddTaskAfter(int timeOut, bool circle, std::function<void()> f) {
        auto t = std::make_shared<task>(genTaskID(), timeOut, circle, f);
        m_mutex.lock();
        insert(t, calTickIndex(m_curr_tick.load(), t->TimeOut));
        m_mutex.unlock();
        return t;
    }
    void    Remove(TaskPtr t) {
        t->Deleted = true;
    }
private:
    void    run() {
        fd_set r;
        FD_SET(m_pair[1], &r);
        struct timeval  durcation;
        int delay = m_time_once;
        if(m_time_once > 1000) {
            durcation.tv_sec = delay / 1000;
            delay -= durcation.tv_sec * 1000;
        }
        durcation.tv_usec = delay * 1000;
        for(;;) {
            int ret = select(m_pair[1] + 1, &r, nullptr, nullptr, &durcation);
            if(ret == -1) { // 异常
                if(errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK) {
                    continue;
                }
                assert(false);
            }else if (ret == 0) { // 超时，检查时间轮当前桶
                int curTickIndex = m_curr_tick.load();
                m_mutex.lock();
                auto head = m_tasks[curTickIndex];
                m_tasks[curTickIndex] = nullptr;
                m_curr_tick.store((curTickIndex + 1) % m_tick_count);
                m_mutex.unlock();

                for(;head != nullptr;) {
                    //cout << " shared_ptr.count = " << head.use_count() << endl;
                    auto cur = head;
                    head = head->Next;

                    cur->Next = nullptr;
                    if(cur->Deleted) {
                        continue;
                    }
                    int threadIndex = incrThreadIndex();
                    if(threadIndex == -1) {
                        // 同步执行任务
                        cur->F();
                        if(cur->Circle) {
                            m_mutex.lock();
                            insert(cur, calTickIndex(curTickIndex, cur->TimeOut));
                            m_mutex.unlock();
                        }
                        continue;
                    }
                    m_threads_queue[threadIndex]->AddTask(0, 0, [this, cur, curTickIndex]{
                        cur->F();
                        if(cur->Circle) {
                            m_mutex.lock();
                            insert(cur, calTickIndex(curTickIndex, cur->TimeOut));
                            m_mutex.unlock();
                        }
                    });
                }
                
            }else { // 收到退出事件
                //cout << "receive exit event" << endl;
                break;
            }
        }
    }

    int     calTickIndex(int curr_tick,int timeOut) {
        int index = static_cast<int>(std::floor(double(timeOut) / m_time_once));
        index = (curr_tick + index) % m_tick_count;
        return index;
    }

    void    insert(TaskPtr t, int insertTickIndex) {
        if(m_tasks[insertTickIndex] == nullptr) {
            m_tasks[insertTickIndex] = t;
        }else {
            t->Next = m_tasks[insertTickIndex];
            m_tasks[insertTickIndex] = t;
        }
    }

    int     incrThreadIndex() {
        if(m_threads_queue.empty()) {
            return -1;
        }
        if(m_round_robin.load() > 100000000) { // 一个亿
            m_round_robin.store(0);
        }else {
            m_round_robin.fetch_add(1);
        }
        return m_round_robin.load() % m_threads_queue.size();
    }

    int64_t genTaskID() {
        if(m_task_id.load(std::memory_order_relaxed) == INT64_MAX) {
            m_task_id.store(0);
        }
        return m_task_id.fetch_add(1, std::memory_order_relaxed);
    }
private:
    std::vector<TaskPtr>      m_tasks;
    std::mutex                m_mutex;
    std::atomic<bool>         m_is_stop;
    const int                 m_tick_count;
    const int                 m_time_once;
    std::atomic<int>          m_curr_tick;
    int                       m_pair[2];
    std::vector<ThreadQueuePtr> m_threads_queue;
    std::atomic<int>          m_round_robin; // 线程任务轮询
    std::thread*              m_ticker;      
    std::atomic<int64_t>      m_task_id;    // 分配任务ID          
};


#endif