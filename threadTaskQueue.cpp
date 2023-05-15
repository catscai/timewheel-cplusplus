#include "threadTaskQueue.h"

ThreadTaskQueue::ThreadTaskQueue(const int& id)
    :m_thread(NULL), m_stop(false),m_lock(),m_cond(),m_timeout(0),m_drop(0),m_id(id) {

}

ThreadTaskQueue::~ThreadTaskQueue() {
    Stop();
}

void ThreadTaskQueue::Start() {
    if(m_thread != NULL) {
        return;
    }
    m_thread = new std::thread(&ThreadTaskQueue::threadaProc, this);
}

void ThreadTaskQueue::AddTask(int64_t nowUnix, int64_t timeout, std::function<void()> f) {
    {
        std::unique_lock<std::mutex> ul(m_lock);
        m_taskq.push_back(std::pair<std::function<void()>, int64_t>(f, nowUnix));
    }
    m_timeout.store(timeout);
    m_cond.notify_one();
}

void ThreadTaskQueue::Stop() {
    if(!m_stop) {
        m_stop = true;
        m_cond.notify_all();
        m_thread->join();
        delete m_thread;
        m_thread = NULL;
    } 
}

size_t ThreadTaskQueue::Size() {
    size_t len = 0;
    {
        std::unique_lock<std::mutex> ul(m_lock);
        len = m_taskq.size();
    }
    return len;
}

int ThreadTaskQueue::ID() {
    return m_id;
}

int64_t ThreadTaskQueue::DropSize() {
    return m_drop.load(std::memory_order_relaxed);
}

void ThreadTaskQueue::threadaProc(void* pvoid) {
    ThreadTaskQueue* pthis = static_cast<ThreadTaskQueue*>(pvoid);
    while(!pthis->m_stop) {
        std::function<void()> f = NULL;
        int64_t fTime = 0;
        {
            std::unique_lock<std::mutex> ul(pthis->m_lock);
            while(pthis->m_taskq.empty() && !pthis->m_stop) {
                pthis->m_cond.wait(ul);
            }
            if(pthis->m_stop) {
                break;
            }
            f = pthis->m_taskq.front().first;
            fTime = pthis->m_taskq.front().second;
            pthis->m_taskq.pop_front();
        }
        if(f != NULL) {
            int64_t timeout = pthis->m_timeout.load(std::memory_order_relaxed);
            if(timeout == 0 || (time(NULL) - fTime) <= timeout) {
                f();
            }else {
                if(pthis->m_drop.fetch_add(1, std::memory_order_relaxed) == INT64_MAX) { 
                    pthis->m_drop.store(0);
                }
            }
        }
    }
}