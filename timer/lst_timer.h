#ifndef TIMER_H
#define TIMER_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include "../log/log.h"

//前向声明
class util_timer;

//连接资源结构体
struct client_data {
    sockaddr_in address;        //客户端地址
    int sockfd;                 //套接字文件描述符
    util_timer* timer;          //定时器
};

//定时器类
class util_timer {
public:
    util_timer() :prev(nullptr), next(nullptr) {}

public:
    time_t expire;                  //任务超时时间，绝对时间

    void(*cb_func)(client_data*);   //回调函数
    client_data* user_data;         //连接资源
    util_timer* prev;
    util_timer* next;
};

class sort_timer_lst {
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer* timer);          //将目标定时器添加到链表中，添加时按照升序添加
    void adjust_timer(util_timer* timer);       //当定时任务发生变化,调整对应定时器在链表中的位置
    void del_timer(util_timer* timer);          //将超时的定时器从链表中删除
    void tick();

private:
    //主要用于调整链表内部结点
    void add_timer(util_timer* timer, util_timer* lst_head);

    util_timer* head;       //定时器链表头
    util_timer* tail;       //链表为
};

class Utils {
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epolfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;           //信号处理的管道文件描述符
    sort_timer_lst m_timer_lst;     //定时器有序链表
    static int u_epolfd;            //epoll实例
    int m_TIMESLOT;                 //定时触发SIGALRM信号
};

void cb_func(client_data* user_data);

#endif