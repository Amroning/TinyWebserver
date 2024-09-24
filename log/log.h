#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include "block_queue.h"
using namespace std;

class Log {
public:
    //单例模式 局部变量懒汉模式
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    //异步写日志的线程函数
    static void* flush_log_thread(void* arg) {
        Log::get_instance()->async_write_log();
    }

    //初始化日志，可选择的参数：日志文件、日志缓冲区大小、最大行数、最长日志条队列
    bool init(const char* file_name, int close_log, int log_buf_size = 8129, int split_lines = 5000000, int max_queue_size = 0);

    //写日志
    void write_log(int level, const char* format, ...);

    //刷新缓冲区
    void flush(void);

private:
    Log();
    virtual ~Log();
    //异步写日志
    void* async_write_log() {
        string single_log;
        //从阻塞队列中取出一个日志string,写入日志文件
        while (m_log_queue->pop(single_log)) {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128];                 //路径名
    char log_name[128];                 //log文件名
    int m_split_lines;                  //日志最大行数
    int m_log_buf_size;                 //日志缓冲区大小
    long long m_count;                  //日志行数记录
    int m_today;                        //按天分类，记录当前时间是哪一天
    FILE* m_fp;                         //log文件指针
    char* m_buf;                        //日志缓冲区
    block_queue<string>* m_log_queue;   //阻塞队列
    bool m_is_async;                    //是否同步标志位
    locker m_mutex;                     //互斥锁
    int m_close_log;                    //关闭日志
};

//日志记录调用
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif