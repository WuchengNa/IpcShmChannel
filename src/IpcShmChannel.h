#ifndef IPC_SHM_CHANNEL_H
#define IPC_SHM_CHANNEL_H

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <chrono>
#include <stdexcept>

enum class Role {
    CLIENT,
    SERVER
};

// 共享内存中的数据结构

struct ChannelData {   
    boost::interprocess::interprocess_mutex mutex;          // 互斥锁
    boost::interprocess::interprocess_condition cond; // 条件变量
    bool is_full;
    size_t size;                       // 数据大小
    char buffer[1024];                 // 数据缓冲区
};

class IpcShmChannel {
public:
    // 构造函数，指定角色和共享内存基础名称
    IpcShmChannel(Role role, const std::string& shm_name);

    // 析构函数，清理资源
    ~IpcShmChannel();

    // 发送数据
    void Send(const char* data, size_t size);

    // 设置接收回调
    void SetRecvCallback(std::function<void(const char*, size_t)> cb);

private:
    // 接收线程循环
    void recv_loop();

    // 创建读写共享内存
    bool create_shared_memory(const std::string& shm_name, boost::interprocess::shared_memory_object& shm, boost::interprocess::mapped_region& region, ChannelData*& channel);

    // 打开只读共享内存
    bool open_shared_memory(const std::string& shm_name, boost::interprocess::shared_memory_object& shm, boost::interprocess::mapped_region& region, ChannelData*& channel);

    Role role;                       // 角色：Client或Server
    std::string shm_name;            // 共享内存基础名称
    std::string write_shm_name;
    std::string read_shm_name;
    
    boost::interprocess::shared_memory_object send_shm;   // 发送共享内存对象
    boost::interprocess::mapped_region send_region;       // 发送共享内存映射
    ChannelData* send_channel;       // 发送通道数据

    boost::interprocess::shared_memory_object recv_shm;   // 接收共享内存对象
    boost::interprocess::mapped_region recv_region;       // 接收共享内存映射
    ChannelData* recv_channel;       // 接收通道数据

    std::function<void(const char*, size_t)> recv_callback; // 接收回调
    std::thread recv_thread;         // 接收线程
    std::atomic<bool> running;       // 运行标志
};

#endif // IPC_SHM_CHANNEL_H