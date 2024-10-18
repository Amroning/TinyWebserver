#include "lst_timer.h"

sort_timer_lst::sort_timer_lst() {
    head = nullptr;
    tail = nullptr;
}

//销毁链表
sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = head;
    while (tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer) {
    if (!timer)
        return;
    if (!head) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }

    add_timer(timer, head);
}

//调整定时器，任务发生变化时，调整定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer* timer) {
    if (!timer)
        return;
    util_timer* tmp = timer->next;
    if (!tmp || timer->expire < tmp->expire)
        return;
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }
    else {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer* timer) {
    if (!timer)
        return;
    if (timer == head && timer == tail) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return;
    }

    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

void sort_timer_lst::tick() {
    if (!head)
        return;

    util_timer* tmp = head;
    time_t cur = time(NULL);                 //获取当前时间
    while (tmp) {
        if (cur < tmp->expire)               //当前时间小于定时器的超时时间，后面的定时器也没有到期
            break;  

        tmp->cb_func(tmp->user_data);        //当前定时器到期，则调用回调函数，执行定时事件

        head = tmp->next;                    //将处理后的定时器从链表容器中删除，并重置头结点
        if (head)
            head->prev = nullptr;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head) {
    util_timer* prev = head;
    util_timer* tmp = prev->next;
    while (tmp) {
        if (timer->expire < tmp->expire) {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }

    if (!tmp) {
        prev->next = timer;
        timer->prev = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epolfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epolfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数    仅仅通过管道发送信号值，不处理信号对应的逻辑，缩短异步执行时间，减少对主程序的影响
void Utils::sig_handler(int sig) {
    //为保证函数的可重入性，保留原来的errno  中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    //将信号值从管道写端写入，传输字符类型，而非整型
    send(u_pipefd[1], (char*)&msg, 1, 0);

    //将原来的errno赋值为当前的errno
    errno = save_errno;
}

//设置信号函数
/*
struct sigaction {
    void (*sa_handler)(int);                            函数指针，指向信号处理函数
    void (*sa_sigaction)(int, siginfo_t *, void *);     同样是信号处理函数，有三个参数，可以获得关于信号更详细的信息
    sigset_t sa_mask;                                   用来指定在信号处理函数执行期间需要被屏蔽的信号
    int sa_flags;                                       用于指定信号处理的行为
    void (*sa_restorer)(void);
}
 */
void Utils::addsig(int sig, void(handler)(int), bool restart) {
    //创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    //信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    //将所有信号添加到信号集中
    sigfillset(&sa.sa_mask);

    //执行sigaction函数
    assert(sigaction(sig, &sa, nullptr) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler() {
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int* Utils::u_pipefd = 0;
int Utils::u_epolfd = 0;

class Utils;
//定时器回调函数
void cb_func(client_data* user_data) {
    //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epolfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    //关闭文件描述符
    close(user_data->sockfd);
    //连接数-1
    http_conn::m_user_count--;
}