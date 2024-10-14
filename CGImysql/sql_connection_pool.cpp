#include "sql_connection_pool.h"
using namespace std;

connection_pool::connection_pool() {
    m_CurConn = 0;
    m_FreeConn = 0;
}

//单例模式   局部静态变量懒汉模式
connection_pool* connection_pool::GetInstance() {
    static connection_pool connPool;
    return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log) {
    m_url = url;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_Port = Port;
    m_close_log = close_log;

    for (int i = 0;i < MaxConn;++i) {
        MYSQL* con = NULL;
        con = mysql_init(con);          //初始化MYSQL结构体变量
        if (con == NULL) {
            LOG_ERROR("MYSQL Error");
            exit(1);
        }

        //连接到MYSQL数据库服务器
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, nullptr, 0);
        if (con == NULL) {
            LOG_ERROR("Mysql Error");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }

    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

//当有请求时，从数据库连接池中返回一个可用连接，更新已使用的和空闲的连接数
MYSQL* connection_pool::GetConnection() {
    MYSQL* con = NULL;

    //连接池无可用连接
    if (0 == connList.size())
        return NULL;

    //信号量
    reserve.wait();

    lock.lock();

    con = connList.front();
    connList.pop_front();
    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();

    return con;
}

//释放当前连接
bool connection_pool::ReleaseConnection(MYSQL* con) {
    if (NULL == con)
        return false;

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post();

    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool() {
    lock.lock();

    if (connList.size() > 0) {
        for (auto it = connList.begin();it != connList.end();++it) {
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_FreeConn = 0;
        m_CurConn = 0;
        connList.clear();
    }

    lock.unlock();
}

//获取当前空闲连接数
int connection_pool::GetFreeConn() {
    return this->m_FreeConn;
}

connection_pool::~connection_pool() {
    DestroyPool();
}

//RAII模式管理数据库连接，确保对象正确获取和释放连接
//构造函数在对象创建时从连接池中获取连接，并存储至成员变量中
//析构函数在对象销毁时将连接归还至连接池中，确保资源被正确释放
connectionRAII::connectionRAII(MYSQL** con, connection_pool* connPool) {
    *con = connPool->GetConnection();

    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
    poolRAII->ReleaseConnection(conRAII);
}