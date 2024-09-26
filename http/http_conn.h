#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <map>
#include <mysql/mysql.h>
#include <fstream>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn {
public:
    static const int FILENAME_LEN = 200;        //文件名长度
    static const int READ_BUFFER_SIZE = 2048;   //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;  //写缓冲区大小

    enum METHOD{        //报文的请求方法
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    enum CHECK_STATE{                   //主状态机状态
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    //请求报文解析结果
    enum HTTP_CODE{             
        NO_REQUEST,             //请求不完整，需要继续读取请求报文数据  跳转主线程继续监测读事件
        GET_REQUEST,            //获得了完整的HTTP请求  调用do_request完成请求资源映射
        BAD_REQUEST,            //HTTP请求报文有语法错误或请求资源为目录    跳转process_write完成响应报文
        NO_RESOURCE,            //请求资源不存在    跳转process_write完成响应报文
        FORBIDDEN_REQUEST,      //请求资源禁止访问，没有读取权限    跳转process_write完成响应报文
        FILE_REQUEST,           //请求资源可以正常访问  跳转process_write完成响应报文
        INTERNAL_ERROR,         //服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    };

    //从状态机状态
    enum LINE_STATUS {
        LINE_OK = 0,        //一行数据成功解析完毕
        LINE_BAD,           //一行数据解析失败
        LINE_OPEN           //一行数据尚未完全接收
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化套接字地址,函数内部会调用私有方法init()
    void init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_log, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    //处理http请求工作函数
    void process();
    //读取浏览器端发来的全部数据
    bool read_once();
    //相应报文写入函数
    bool write();

    //获取存储客户端地址信息的指针
    sockaddr_in* get_address() {
        return &m_address;
    }

    //同步线程初始化数据库读取表
    void initmysql_result(connection_pool* connPool);
    int timer_flag;         //标记请求是否需要重新设置计时器或处理超时
    int improv;             //标记请求是否得到了处理

private:
    void init();
    //从m_real_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向写缓冲区m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
    //主状态机解析请求报文 请求行数据
    HTTP_CODE parse_request_line(char* text);
    //主状态机解析请求报文 请求头数据s
    HTTP_CODE parse_headers(char* text);
    //主状态机解析请求报文 请求内容
    HTTP_CODE parse_content(char* text);
    //生成相应报文
    HTTP_CODE do_request();

    //获取读缓冲区中未处理的字符
    //m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
    //此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
    char* get_line() { return m_read_buf + m_start_line; };

    //从状态机 读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    void unmap();

    //生成相应报文，由do_request()调用
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL* mysql;
    int m_state;        //读为0，写为1

private:
    int m_sockfd;                          //与客户端通信的套接字文件描述符
    sockaddr_in m_address;                 //存储客户端的地址信息

    
    char m_read_buf[READ_BUFFER_SIZE];     //读缓冲区 存储读取的请求报文数据
    long m_read_idx;                        //读缓冲区中数据的最后一个字节的下一个位置
    long m_checked_idx;                    //在读缓冲区中读取的位置
    int m_start_line;                      //已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];   //写缓冲区 存储发出的响应报文数据
    int m_write_idx;                       //写缓冲区中数据的长度

    CHECK_STATE m_check_state;             //主状态机状态
    METHOD m_method;                       //请求方法

    //请求报文中对应的6个变量
    char m_real_file[FILENAME_LEN];        //读取文件的名称
    char* m_url;                           //请求资源的路径
    char* m_version;                       //http版本
    char* m_host;                          //请求的主机名
    long m_content_length;                  //请求报文的内容长度
    bool m_linger;                         //是否保持连接

    char* m_file_address;                  //内存地址  通过mmap将文件映射到内存中
    struct stat m_file_stat;               //文件的状态信息
    struct iovec m_iv[2];                  //io向量机制iovec（指向缓冲区的指针、缓冲区大小）
    int m_iv_count;                        //使用了几个iovec结构体
    int cgi;                               //是否启用POST
    char* m_string;                        //请求头数据
    int bytes_to_send;                     //剩余发送字节数
    int bytes_have_send;                   //已发送字节数
    char* doc_root;                        //服务器文档根目录

    map<string, string>m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif