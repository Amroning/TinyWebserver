/*
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template<typename T>
class block_queue {
public:
    //构造函数，初始化私有成员
    block_queue(int max_size = 1000) {
        if (max_size <= 0)
            exit(-1);

        m_max_size = max_size;
        m_array = new T[max_size];      //创建循环数组
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue() {
        m_mutex.lock();
        if (m_array != NULL)
            delete[] m_array;
        m_mutex.unlock();
    }

    //判断队列是否已满
    bool full(){
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //返回队首元素
    bool front(T& value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }

        value = m_array[(m_front + 1) % m_max_size];

        m_mutex.unlock();

        return true;
    }

    //返回队尾元素
    bool back(T& value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }

        value = m_array[m_back];

        m_mutex.unlock();

        return true;
    }

    //返回当前队列大小
    int size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();

        return tmp;
    }

    //返回队列最大容量
    int max_size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();

        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T& item) {
        m_mutex.lock();

        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;       //新增数据放在循环数组对应位置
        m_array[m_back] = item;
        m_size++;
        m_cond.broadcast();
        m_mutex.unlock();

        return true;
    }

    //pop时，如果当前队列没有元素，将会等待条件变量
    bool pop(T& item) {
        m_mutex.lock();

        while (m_size <= 0) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;       
        item = m_array[m_front];                    //用item保存取出的队首元素
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //超时处理  在指定时间内抢互斥锁
    bool pop(T& item, int ms_timeout) {
        struct timespec t = { 0,0 };
        struct timeval now = { 0,0 };

        gettimeofday(&now, NULL);
        m_mutex.lock();

        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;     //互斥锁
    cond m_cond;        //条件变量

    T* m_array;         //队列数组
    int m_size;         //当前队列大小
    int m_max_size;     //队列最大容量
    int m_front;        //队首索引
    int m_back;         //队尾索引
};

#endif