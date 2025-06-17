#include "IpcShmChannel.h"

using namespace boost::interprocess;


bool IpcShmChannel::create_shared_memory(const std::string& shm_name, shared_memory_object& shm, mapped_region& region, ChannelData*& channel) {
    shm = shared_memory_object(create_only, shm_name.c_str(), read_write);
    shm.truncate(sizeof(ChannelData));
    region = mapped_region(shm, read_write);
    new (region.get_address()) ChannelData();
    auto* channel_test = new ChannelData();
    channel = static_cast<ChannelData*>(region.get_address());
    channel->is_full = false;
    return true;
}

bool IpcShmChannel::open_shared_memory(const std::string& shm_name, shared_memory_object& shm, mapped_region& region, ChannelData*& channel) {
    while (running) {
        try {
            shm = shared_memory_object(open_only, shm_name.c_str(), read_only);
            break;
        } catch (interprocess_exception& e) {
            if (e.get_error_code() == not_found_error) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                return false;
            }
        }
    }
    region = mapped_region(shm, read_only);
    if (region.get_size() < sizeof(ChannelData)) {
        throw std::runtime_error("Shared memory size is too small");
    }

    channel = static_cast<ChannelData*>(region.get_address());
    return true;
}

// 构造函数，指定角色和共享内存基础名称
IpcShmChannel::IpcShmChannel(Role role, const std::string &shm_name)
    : role(role), shm_name(shm_name), running(true)
{
     // SERVER: "_up" 是只读（接收），"_down" 是读写（发送）
     // Client: "_up" 是读写（发送），"_down" 是只读（接收）
    write_shm_name =(role == Role::CLIENT)? shm_name + "_up" : shm_name + "_down";   
    shared_memory_object::remove(write_shm_name.c_str());

    create_shared_memory(write_shm_name, send_shm, send_region, send_channel);

    // 启动接收线程
    recv_thread = std::thread(&IpcShmChannel::recv_loop, this);
}

// 析构函数，清理资源
IpcShmChannel::~IpcShmChannel()
{
    running = false;
    recv_channel->cond_receiver.notify_one();
    recv_thread.join();

    //释放共享内存
    shared_memory_object::remove(write_shm_name.c_str());
}

// 发送数据
void IpcShmChannel::Send(const char *data, size_t size)
{
    if (size > 1024)
    {
        throw std::runtime_error("OverFlow");
    }
    scoped_lock<interprocess_mutex> lock(send_channel->mutex);
    while (send_channel->is_full)
    {
        send_channel->cond_sender.wait(lock);
    }
    memcpy(send_channel->buffer, data, size);
    send_channel->size = size;
    send_channel->is_full = true;
    send_channel->cond_receiver.notify_one();
}

// 设置接收回调
void IpcShmChannel::SetRecvCallback(std::function<void(const char *, size_t)> cb)
{
    recv_callback = cb;
}

void IpcShmChannel::recv_loop()
{
    std::this_thread::sleep_for(std::chrono::seconds(10));

    read_shm_name =(role == Role::CLIENT) ? shm_name + "_down" : shm_name + "_up";
    open_shared_memory(read_shm_name, recv_shm, recv_region, recv_channel);

    while (running)
    {
        scoped_lock<interprocess_mutex> lock(recv_channel->mutex);
        while (!recv_channel->is_full && running)
        {
            recv_channel->cond_receiver.wait(lock);
        }
        if (!running)
            break;
        char data[1024];
        size_t msg_size = recv_channel->size;
        memcpy(data, recv_channel->buffer, msg_size);
        recv_channel->is_full = false;
        recv_channel->cond_sender.notify_one();
        if (recv_callback)
        {
            recv_callback(data, msg_size);
        }
    }
}
