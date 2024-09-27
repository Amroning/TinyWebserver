#include "webserver.h"

//Webserver类构造函数
WebServer::WebServer() {
    //http_conn类对象   存储连接的客户端
    users = new http_conn[MAX_FD];

    //配置root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

//Webserver类析构函数
WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//初始化相关变量
void WebServer::init(int port, string user, string passWord, string databaseName,
    int log_write, int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model) {
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

//设置服务器的触发模式
//根据m_TRIGMode的值设置监听套接字和连接套接字的触发模式
void WebServer::trig_mode() {
    //LT + LT
    if (0 == m_TRIGMode){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//初始化日志
void WebServer::log_write() {
    if (0 == m_close_log) {
        //初始化日志
        if (1 == m_log_write)       //异步写日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else                        //同步写日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

//初始化数据库相关
void WebServer::sql_pool() {
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

//初始化线程池
void WebServer::thread_pool() {
    //初始化线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

//服务器的初始化和准备工作（网络套接字的配置、epoll实例、设置信号处理）
void WebServer::eventListen() {
    //监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //SO_LINGER控制套接字关闭的行为     close套接字的时候，是否立即关闭套接字、是否等待套接字中的数据发送完毕
    /*

    struct linger
    {
        int l_onoff;		是否启用SO_LINGER
        int l_linger;		等待时间
    };

    */
    if (0 == m_OPT_LINGER) {            
        struct linger tmp = { 0,1 };    //关闭套接字时不等待数据发送完毕，直接关闭
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) {       //关闭套接字时最多等待1秒，之后关闭套接字
        struct linger tmp = { 1,1 };
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    //设置SO_REUSEADDR
    int ret = 0, flag = 1;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    //绑定
    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    //开始监听
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    epoll_event event[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    //m_pipefd 信号处理的管道文件描述符,写端设置非阻塞，读端注册到epoll实例中，用于监听管道数据
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    //设置相应的信号处理
    utils.addsig(SIGPIPE, SIG_IGN);                     //忽略SIGPIPE（通常在向一个已经关闭的管道或套接字写入数据时产生），可以防止程序因写入已关闭的管道或套接字而崩溃
    utils.addsig(SIGALRM, utils.sig_handler, false);    //SIGALRM信号由alarm函数触发，用于定时操作  产生该信号时写入信号处理管道
    utils.addsig(SIGTERM, utils.sig_handler, false);    //SIGTERM信号用于请求程序终止   产生该信号时写入信号处理管道

    //定时产生SIGALRM信号
    alarm(TIMESLOT);

    //设置Utils类中的信号管道描述符和epoll文件描述符
    Utils::u_pipefd = m_pipefd;
    Utils::u_epolfd = m_epollfd;
}

//初始化定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    util_timer* timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s", "adjust timer once");
}

//计时器到期，调用回调函数关闭连接描述符,删除计时器
void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
        utils.m_timer_lst.del_timer(timer);

    LOG_INFO("clsoe fd %d", users_timer[sockfd].sockfd);
}

//处理客户端连接请求
bool WebServer::dealclientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    //水平触发
    if (0 == m_LISTENTrigmode) {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if (connfd < 0) {
            LOG_ERROR("%s :errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    //边缘触发
    else {
        while (1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);     //当没有连接时，accept返回-1，connfd<0，跳出循环
            if (connfd < 0) {
                LOG_ERROR("%s :errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD) {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

//处理SIGALRM信号和SIGTERM信号
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
    char signals[1024];
    int ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret <= 0)
        return false;
    else {
        for (int i = 0;i < ret;++i) {
            switch (signals[i]) {
                case SIGALRM: {
                    timeout = true;
                    break;
                }
                case SIGTERM: {
                    stop_server = true;
                    break;
                }
            }
        }
    }

    return true;
}

//根据不同模型处理读事件
void WebServer::dealwithread(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel) {
        if (timer)
            adjust_timer(timer);

        //若监测到读事件，将该事件加入请求队列
        m_pool->append(users + sockfd, 0);

        //improv和timer_flag同时为1时，表示请求处理失败，需要关闭连接并删除定时器，以释放资源并防止资源泄漏
        while (true) {
            if (1 == users[sockfd].improv) {
                if (1 == users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;       //避免旧的处理状态影响新的请求处理逻辑
                break;
            }
        }
    }
    //proactor
    else {
        if (users[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件加入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
                adjust_timer(timer);
        }
        else
            deal_timer(timer, sockfd);
    }
}

//根据不同模型处理写事件
void WebServer::dealwithwrite(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel) {
        if (timer)
            adjust_timer(timer);

        m_pool->append(users + sockfd, 1);

        while (true) {
            if (1 == users[sockfd].improv) {
                if (1 == users[sockfd].timer_flag) {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor
    else {
        if (users[sockfd].write()) {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
                adjust_timer(timer);
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}

//核心事件循环 处理新的连接请求、关闭连接、信号、读写事件和定时处理过期连接
void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server) {
        int nums = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (nums < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0;i < nums;++i) {
            int sockfd = events[i].data.fd;

            //处理新的连接请求
            if (sockfd == m_listenfd) {
                bool ret = dealclientdata();
                if (!ret)
                    continue;
            }
            //服务器端关闭连接，移除对应计时器
            //EPOLLRDHUP    对端关闭写端（不再发送数据）
            //EPOLLHUP      连接已终止或失效
            //EPOLLERR      文件描述符上发生了错误
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号事件
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool ret = dealwithsignal(timeout, stop_server);
                if (!ret)
                    LOG_ERROR("%s", "deal with signal failure");
            }
            //处理读事件
            else if (events[i].events & EPOLLIN) {
                dealwithread(sockfd);
            }
            //处理写事件
            else if (events[i].events & EPOLLOUT) {
                dealwithwrite(sockfd);
            }
        }
        //定时发出SIGALRM信号，检查过期连接
        if (timeout) {
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}