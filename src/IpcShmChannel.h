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

#define DEFULT_BUFFER_CAP 1024

enum class Role {
    CLIENT,
    SERVER
};

// 共享内存中的数据结构

class ChannelData;

typedef std::function<void(int sender_pid, const char* data, size_t data_size)> RecvDataCallback;
class IpcShmChannel {
public:
    // 构造函数，指定角色和共享内存基础名称
    IpcShmChannel(Role role, const std::string& shm_name);

    // 析构函数，清理资源
    ~IpcShmChannel();

    bool IsRunning() const { return running_; }

    Role GetRole() const { return role_; }

    bool Start();

    void Stop();

    // 发送数据
    bool Send(const char* data, size_t size);

    // 设置接收回调
    void SetRecvCallback(RecvDataCallback cb);

private:
    // 接收线程循环
    void recv_loop();

    // 创建读写共享内存
    bool create_shared_memory(const std::string& shm_name, boost::interprocess::shared_memory_object& shm, boost::interprocess::mapped_region& region, ChannelData*& channel);

    // 打开只读共享内存
    bool open_shared_memory(const std::string& shm_name, boost::interprocess::shared_memory_object& shm, boost::interprocess::mapped_region& region, ChannelData*& channel);

    Role role_;                       // 角色：Client或Server
    size_t buffer_size_;              // 缓冲区大小
    std::string shm_name_;            // 共享内存基础名称
    std::string write_shm_name_;
    std::string read_shm_name_;
    
    boost::interprocess::shared_memory_object send_shm_;   // 发送共享内存对象
    boost::interprocess::mapped_region send_region_;       // 发送共享内存映射
    ChannelData* send_channel_;       // 发送通道数据

    boost::interprocess::shared_memory_object recv_shm_;   // 接收共享内存对象
    boost::interprocess::mapped_region recv_region_;       // 接收共享内存映射
    ChannelData* recv_channel_;       // 接收通道数据

    RecvDataCallback recv_callback_; // 接收回调
    std::thread recv_thread_;         // 接收线程
    std::atomic<bool> running_;       // 运行标志
};

#endif // IPC_SHM_CHANNEL_H