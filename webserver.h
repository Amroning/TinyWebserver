#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <assert.h>
#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65535;               //最大文件描述符
const int MAX_EVENT_NUMBER = 10000;     //最大事件数
const int TIMESLOT = 5;                 //最小超时单位

class WebServer{
public:
    WebServer();
    ~WebServer();

    //初始化相关变量
    void init(int port, string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();            //初始化线程池
    void sql_pool();               //初始化数据库
    void log_write();              //初始化日志
    void trig_mode();              //设置服务器的触发模式
    void eventListen();            //服务器的初始化和准备工作（网络套接字的配置、epoll实例、设置信号处理）
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);      //初始化定时器
    void adjust_timer(util_timer *timer);                           //调整定时器在链表上的位置
    void deal_timer(util_timer *timer, int sockfd);                 //计时器到期，调用回调函数关闭连接描述符,删除计时器
    bool dealclientdata();                                          //处理客户端连接请求
    bool dealwithsignal(bool& timeout, bool& stop_server);          //处理SIGALRM信号和SIGTERM信号
    void dealwithread(int sockfd);                                  //根据不同模型处理读事件
    void dealwithwrite(int sockfd);                                 //根据不同模型处理写事件

public:
    //基础
    int m_port;                             //服务器端监听端口号
    char *m_root;                           //服务器资源根目录
    int m_log_write;                        //m_log_write=0为同步写日志     m_log_write>0为阻塞队列长度，异步写日志
    int m_close_log;                        //是否关闭日志
    int m_actormodel;                       //反应堆模型

    int m_pipefd[2];                        //信号处理的管道文件描述符 m_pipefd[0]读端  m_pipefd[1]写端
    int m_epollfd;                          //epoll文件描述符
    http_conn *users;                       //连接的客户端

    //数据库相关
    connection_pool *m_connPool;
    string m_user;                          //登陆数据库用户名
    string m_passWord;                      //登陆数据库密码
    string m_databaseName;                  //使用数据库名
    int m_sql_num;                          //数据库最大连接数

    //线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;                 //监听套接字
    int m_OPT_LINGER;               //设置套接字SO_LINGER属性
    int m_TRIGMode;                 //设置监听套接字和连接套接字的触发模式
    int m_LISTENTrigmode;           //监听套接字的触发模式  0-LT，1-ET
    int m_CONNTrigmode;             //连接套接字的触发模式  0-LT，1-ET

    //定时器相关
    client_data *users_timer;       
    Utils utils;
};


#endif