#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include "../lock/locker.h"

//线程池
template<typename T>
class threadpool {

public:
    threadpool(int actor_model, connection_pool* connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    //工作线程运行的函数，不断从工作队列中取出任务并执行
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;            //线程池中的线程数
    int m_max_request;              //请求队列中允许的最大请求数
    pthread_t* m_threads;           //描述线程池的数组，大小为m_thread_number
    std::list<T*>m_workqueue;       //请求队列
    locker m_queuelocker;           //保护请求队列的互斥锁
    sem m_queuestat;                //信号量  是否有任务需要处理
    connection_pool* m_connPool;    //数据库
    int m_actor_model;              //模型切换
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool* connPool, int thread_number, int max_request) :
    m_actor_model(actor_model), m_connPool(connPool), m_thread_number(thread_number), m_max_request(max_request), m_threads(nullptr) {
    if (thread_number <= 0 || max_request <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    for (int i = 0;i < m_thread_number;++i) {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
}

template<typename T>
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    //检查当前请求队列是否已满
    if (m_workqueue.size() >= m_max_request) {
        m_queuelocker.unlock();
        return false;
    }
    //添加请求至队列中
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();
    //检查当前请求队列是否已满
    if (m_workqueue.size() >= m_max_request) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while (true) {
        m_queuestat.wait();
        m_queuelocker.lock();
        //检查请求队列是否为空
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        //取出请求开始处理
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request)
            continue;

        
    }
}

#endif