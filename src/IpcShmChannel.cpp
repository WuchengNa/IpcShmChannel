#include "IpcShmChannel.h"

#ifdef _WINDOWS
    #include <windows.h>
#else
    #include <unistd.h>
#endif

using namespace boost::interprocess;

class ChannelData {
public:
    ChannelData()
        :new_msg(false)
        , size(0)
        , owner_pid(0)
        , sender_working(true)
        , revcer_working(false)
        , ev_type(-1)
    {
        // 初始化缓冲区
        memset(buffer, 0, sizeof(buffer));
    }

    ~ChannelData()
    {

    }

    boost::interprocess::interprocess_mutex mutex;          // 互斥锁
    boost::interprocess::interprocess_condition cond; // 条件变量

    bool   new_msg;
    bool   sender_working;
    bool   revcer_working;
    int    owner_pid;
    short  ev_type;
    size_t size;                       // 数据大小
    char buffer[DEFULT_BUFFER_CAP];                 // 数据缓冲区
};

bool IpcShmChannel::create_shared_memory(const std::string& shm_name, shared_memory_object& shm, mapped_region& region, ChannelData*& channel) {
    
    try {
        shm = shared_memory_object(create_only, shm_name.c_str(), read_write);
    }
    catch (interprocess_exception& e) {
        if (e.get_error_code() == already_exists_error) {
            shared_memory_object::remove(shm_name.c_str());
            shm = shared_memory_object(create_only, shm_name.c_str(), read_write);
        }
        else {
            return false; // 其他错误
        }
    }
    shm.truncate(sizeof(ChannelData));
    region = mapped_region(shm, read_write);
    new (region.get_address()) ChannelData();
    channel = static_cast<ChannelData*>(region.get_address());
#ifdef _WINDOWS
    channel->owner_pid = GetCurrentProcessId();
#else
    channel->owner_pid = getpid();
#endif
    send(role_ == Role::CLIENT ? EventType::client_hello:EventType::server_hello, 0, 0);
    return true;
}

bool IpcShmChannel::open_shared_memory(const std::string& shm_name, shared_memory_object& shm, mapped_region& region, ChannelData*& channel) {
    while (running_) {
        try {
            shm = shared_memory_object(open_only, shm_name.c_str(), read_write);
            break;
        } catch (interprocess_exception& e) {
            if (e.get_error_code() == not_found_error) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                return false;
            }
        }
    }
    region = mapped_region(shm, read_write);
    if (region.get_size() < sizeof(ChannelData)) {
        return false;
    }

    channel = static_cast<ChannelData*>(region.get_address());
    channel->revcer_working = true;
    channel->cond.notify_one();

    return true;
}

void IpcShmChannel::notify(EventType evt, int sender_pid, const char* data, size_t size)
{
    if (recv_callback_)
    {
        recv_callback_(evt, sender_pid, data, size);
    }
}

// 构造函数，指定角色和共享内存基础名称
IpcShmChannel::IpcShmChannel(Role role, const std::string &shm_name)
    : role_(role), shm_name_(shm_name), running_(true)
{
     // SERVER: "_up" 是只读（接收），"_down" 是读写（发送）
     // Client: "_up" 是读写（发送），"_down" 是只读（接收）
    write_shm_name_ =(role == Role::CLIENT)? shm_name + "_up" : shm_name + "_down";
    read_shm_name_ = (role == Role::CLIENT) ? shm_name + "_down" : shm_name + "_up";
    if (role == Role::SERVER) {
        // 释放共享内存
        shared_memory_object::remove(write_shm_name_.c_str());
        shared_memory_object::remove(read_shm_name_.c_str());
    }
}

// 析构函数，清理资源
IpcShmChannel::~IpcShmChannel()
{
    Stop();
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    // 释放映射区域
    send_region_ = mapped_region();

    // 释放共享内存
    shared_memory_object::remove(write_shm_name_.c_str());
}

bool IpcShmChannel::Start()
{
    if(!create_shared_memory(write_shm_name_, send_shm_, send_region_, send_channel_))
        return false;

    // 启动接收线程
    recv_thread_ = std::thread(&IpcShmChannel::recv_loop, this);
    return true;
}


void IpcShmChannel::Stop()
{
    if(running_)
    {
        running_ = false;

        if(send_channel_ != nullptr){        
            send_channel_->sender_working = false;
            send_channel_->cond.notify_all();
        }

        try
        {
            if (recv_channel_ != nullptr) {
                recv_channel_->revcer_working = false;
                recv_channel_->cond.notify_all();
            }
        }
        catch (boost::interprocess::interprocess_exception& e)
        {
            if (e.get_error_code() == boost::interprocess::error_code_t::not_such_file_or_directory)
                return;
            throw e;
        }
    }
}

// 设置接收回调
void IpcShmChannel::SetRecvCallback(RecvDataCallback cb)
{
    recv_callback_ = cb;
}

bool IpcShmChannel::send(EventType evt, const char* data, size_t size)
{
    if (size > DEFULT_BUFFER_CAP)
    {
        return false; // 数据大小超过缓冲区容量
    }
    scoped_lock<interprocess_mutex> lock(send_channel_->mutex);
    send_channel_->cond.wait(lock, [this]() {return !send_channel_->revcer_working || !send_channel_->new_msg || !running_; });
    if (!running_ || !send_channel_->revcer_working) {
        lock.unlock();
        send_channel_->cond.notify_all();
        return false;
    }
    //memset(send_channel_->buffer, 0, DEFULT_BUFFER_CAP);
    memcpy(send_channel_->buffer, data, size);
    send_channel_->size = size;
    send_channel_->new_msg = true;
    send_channel_->ev_type = (short)evt;
    lock.unlock(); // 解锁互斥锁，允许其他线程接收数据
    send_channel_->cond.notify_all();
    return true;
}

bool IpcShmChannel::SendTextMsg(const char* data, size_t size)
{
    return send(EventType::msg_text,data,size);
}

bool IpcShmChannel::SendJsonMsg(const char* data, size_t size) {
    return send(EventType::msg_json, data, size);
}

bool IpcShmChannel::SendPbMsg(const char* data, size_t size)
{
    return send(EventType::msg_pb, data, size);
}

void IpcShmChannel::recv_loop()
{
    if (!open_shared_memory(read_shm_name_, recv_shm_, recv_region_, recv_channel_)) {
        return;
    }

    notify(EventType::start_listen, 0, 0, 0);
    
    char data_tmp[DEFULT_BUFFER_CAP];
    size_t data_size= 0;
    int sender_pid = 0;
    EventType type = EventType::start_listen;
    while (running_)
    {
        scoped_lock<interprocess_mutex> lock(recv_channel_->mutex);  
        recv_channel_->cond.wait(lock, [this]() { return !recv_channel_->sender_working || recv_channel_->new_msg || !running_; });

        if (!running_ || !recv_channel_->sender_working) {
            lock.unlock();
            recv_channel_->cond.notify_one();
            break;
        } 
        sender_pid = recv_channel_->owner_pid;
        data_size = recv_channel_->size;
        type = (EventType)recv_channel_->ev_type;
        memcpy(data_tmp, recv_channel_->buffer, data_size);
        recv_channel_->new_msg = false;

        lock.unlock(); // 解锁互斥锁，允许其他线程发送数据
        recv_channel_->cond.notify_one();
        notify(type, sender_pid, data_tmp, data_size);
    }
}
