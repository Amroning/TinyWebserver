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
    m_MaxConn = MaxConn;
    m_close_log = close_log;

    for (int i = 0;i < m_MaxConn;++i) {
        MYSQL* con = NULL;
        con = mysql_init(con);
        if (con == NULL) {
            
        }
    }
}