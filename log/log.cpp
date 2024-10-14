#include "log.h"
using namespace std;

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if (m_fp != NULL) {
        fclose(m_fp);
    }
}

//初始化函数，异步需要设置阻塞队列长度，同步不需要
bool Log::init(const char* file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size) {
    //如果设置了阻塞队列长度，则设置为异步
    if (max_queue_size >= 1) {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //类中的flush_log_thread为回调函数，表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    //初始化参数
    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    //获取本地时间
    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //生成包含日期的日志文件名
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = { 0 };
    if (p == NULL) {        //若没有目录路径，则直接由file_name生成日志文件名（时间+文件名）
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {                  //若有目录路径，则在之前加上时间（路径+时间+文件名）
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    //以追加模式打开文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
        return false;

    return true;
}

void Log::write_log(int level, const char* format, ...) {
    //获取本地时间
    struct timeval now = { 0,0 };
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //获取写入日志的类型
    char s[16] = { 0 };
    switch (level) {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[error]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    //管理日志文件的写入  当日期发生变化或者日志文件达到一定大小就创建新的日志文件
    m_mutex.lock();
    m_count++;

    //日期变化时、日志行数达到最大值时，需要创建新的日志文件
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = { 0 };
        fflush(m_fp);               //刷新文件流的输出缓冲区，将缓冲区中的数据立即写入到文件中
        fclose(m_fp);               //并关闭当前日志文件
        char tail[16] = { 0 };      //存储日期信息

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday) {         //如果是日期发生变化,创建新的日志文件，名字格式为 dir_name + tail + log_name
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;            //更新日期
            m_count = 0;                        //重置计数器
        }
        else {                                  //如果是日志行数达到最大值，创建新的日志文件，名字格式为 dir_name + tail + log_name + 分割编号
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;             //存储格式化后的日志内容
    m_mutex.lock();

    //写入的具体时间 内容格式
    int n = snprintf(m_buf, 48, "%d_%02d_%02d %02d:%02d:%02d.%06ld %s",
        my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
        my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);             //snprintf将内容格式化至缓冲区

    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);        //vsnprintf讲格式化内容追加至缓冲区
    m_buf[n + m] = '\n';                //行末添加换行符
    m_buf[n + m + 1] = '\0';            //添加字符串结束符
    log_str = m_buf;                    //存储日志内容

    m_mutex.unlock();

    if (m_is_async && !m_log_queue->full()) {       //异步写入日志
        m_log_queue->push(log_str);
    }
    else {                                          //同步写入日志
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void) {
    m_mutex.lock();
    fflush(m_fp);       //强制刷新写入流缓冲区
    m_mutex.unlock();
}