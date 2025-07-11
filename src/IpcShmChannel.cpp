#include "IpcShmChannel.h"

#ifdef _WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <iostream>
#include <chrono>

#define HEART_BEAT_TIME_INTERVAL 1
#define MAX_HEART_BEAT_FAILED_TIME 3
#define CHANNEL_TIME_OUT 3

using namespace boost::interprocess;

// 定义DataPacket的前缀
// |type(1 byte)|data total length(3 byte)|chunk seq (2 byte)|chunk total count(2 byte)|data chunk(Max(DEFULT_BUFFER_CAP))|

struct DataPacketHeader
{
    short type;         // 实际只用1字节
    size_t totalLen;       // 实际只用3字节，消息的总大小
    short chunkSeq;     // 2字节，当前块的序列
    short chunkTotalCount; // 2字节，分块的总数
    long  msgSeq;       //4字节,标识当前chunk所属的消息seq

    // 将头部信息写入buffer
    void SetHeader(char* buffer) const {
        buffer[0] = static_cast<char>(type & 0xFF);
        // totalLen 占3字节（大端序）
        buffer[1] = static_cast<char>((totalLen >> 16) & 0xFF);
        buffer[2] = static_cast<char>((totalLen >> 8) & 0xFF);
        buffer[3] = static_cast<char>(totalLen & 0xFF);
        // chunkSeq 占2字节（大端序）
        buffer[4] = static_cast<char>((chunkSeq >> 8) & 0xFF);
        buffer[5] = static_cast<char>(chunkSeq & 0xFF);
        // chunkTotalCount 占2字节（大端序）
        buffer[6] = static_cast<char>((chunkTotalCount >> 8) & 0xFF);
        buffer[7] = static_cast<char>(chunkTotalCount & 0xFF);

        buffer[8] = static_cast<char>((msgSeq >> 24) & 0xFF);
        buffer[9] = static_cast<char>((msgSeq >> 16) & 0xFF);
        buffer[10]= static_cast<char>((msgSeq >> 8) & 0xFF);
        buffer[11] = static_cast<char>(msgSeq & 0xFF);
    }

    // 从buffer解析出头部信息
    void GetHeader(char* buffer) {
        type = static_cast<unsigned char>(buffer[0]);
        // totalLen 占3字节（大端序）
        totalLen = (static_cast<unsigned char>(buffer[1]) << 16) |
                   (static_cast<unsigned char>(buffer[2]) << 8) |
                   (static_cast<unsigned char>(buffer[3]));
        // chunkSeq 占2字节（大端序）
        chunkSeq = (static_cast<unsigned char>(buffer[4]) << 8) |
                   (static_cast<unsigned char>(buffer[5]));
        // chunkTotalCount 占2字节（大端序）
        chunkTotalCount = (static_cast<unsigned char>(buffer[6]) << 8) |
                          (static_cast<unsigned char>(buffer[7]));

        msgSeq = (static_cast<unsigned char>(buffer[8]) << 24) |
                (static_cast<unsigned char>(buffer[9]) << 16) |
                (static_cast<unsigned char>(buffer[10]) << 8) |
                (static_cast<unsigned char>(buffer[11]));
    }
};
#define DataPacketPrefixSize 12 // 前缀大小：1 + 3 + 2 + 2 + 4 = 12 bytes

typedef std::shared_ptr<DataPacket> DataPacketPtr;
struct DataPacket
{
    IpcShmChannel::EventType evt;
    DataPacketHeader header;
    const char* data = nullptr;
    size_t data_size = 0;

    static  DataPacketPtr make(IpcShmChannel::EventType evt, const DataPacketHeader* header, const char* data, size_t data_size) {
        return std::make_shared<DataPacket>(evt,header,data,data_size);
    }

    DataPacket(IpcShmChannel::EventType e, const DataPacketHeader* h, const char* d, size_t d_s) {
        evt = e;
        if (h != nullptr)
            header = *h;
        if (d != nullptr) {
            data = new char[d_s];
            data_size = d_s;
            memcpy((void*)data, d, data_size);
        }
    }

    DataPacket(const DataPacket& other)
        :evt(other.evt)
        ,header(other.header)
        ,data_size(other.data_size)
    {
        if (other.data != nullptr && other.data_size != 0) {
            data = new char [data_size];
            memcpy((void*)data, other.data, data_size);
        }
    }

    ~DataPacket() {
        if (data != nullptr) {
            delete[] data;
            data = nullptr;
            data_size = 0;
        }
    }
};

class ChannelData
{
public:
    ChannelData()
        : new_msg(false), size(0), owner_pid(0), sender_working(false), recver_working(false), ev_type(-1)
    {
        // 初始化缓冲区
        memset(buffer, 0, sizeof(buffer));
    }

    ~ChannelData()
    {
    }

    boost::interprocess::interprocess_mutex mutex;    // 互斥锁
    boost::interprocess::interprocess_condition cond; // 条件变量

    bool new_msg;
    bool sender_working;
    bool recver_working;
    int owner_pid;
    short ev_type;
    size_t size;                    // 数据大小
    char buffer[DataPacketPrefixSize+DEFULT_BUFFER_CAP]; // 数据缓冲区
};

bool IpcShmChannel::create_shared_memory(const std::string &shm_name, shared_memory_object &shm, mapped_region &region, ChannelData *&channel)
{

    try
    {
        shm = shared_memory_object(create_only, shm_name.c_str(), read_write);
    }
    catch (interprocess_exception &e)
    {
        if (e.get_error_code() == already_exists_error)
        {
            shared_memory_object::remove(shm_name.c_str());
            shm = shared_memory_object(create_only, shm_name.c_str(), read_write);
        }
        else
        {
            return false; // 其他错误
        }
    }
    shm.truncate(sizeof(ChannelData));
    region = mapped_region(shm, read_write);
    new (region.get_address()) ChannelData();
    channel = static_cast<ChannelData *>(region.get_address());
#ifdef _WINDOWS
    channel->owner_pid = GetCurrentProcessId();
#else
    channel->owner_pid = getpid();
#endif
    return true;
}


bool IpcShmChannel::open_shared_memory(const std::string &shm_name, shared_memory_object &shm, mapped_region &region, ChannelData *&channel)
{
    while (running_)
    {
        try
        {
            shm = shared_memory_object(open_only, shm_name.c_str(), read_write);
            region = mapped_region(shm, read_write);
            if (region.get_size() < sizeof(ChannelData))
            {
                return false;
            }

            channel = static_cast<ChannelData*>(region.get_address());
            return true;
        }
        catch (interprocess_exception &e)
        {
            if (e.get_error_code() == not_found_error)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else
            {
                return false;
            }
        }
    }
    return false;
}

void IpcShmChannel::notify(EventType evt, int sender_pid, const char *data, size_t size)
{
    if (evt == EventType::start_listen)
    {
        set_recv_channel_status(true);
        std::cout << "on start listen" << std::endl;
    }
    else if(evt == EventType::client_hello || evt == EventType::server_hello)
    {
        std::cout << "on hello" << std::endl;
        set_send_channel_status(true);
        pend_pong_count_.store(0);
    }
    else if (evt == EventType::client_byebye || evt == EventType::server_byebye) {
    }
    else if (evt == EventType::ping) {
        pong();
    }
    else if (evt == EventType::pong) {
        pend_pong_count_--;
    }
    else if (evt == EventType::stop_listen) {
        recv_channel_connected_.store(false);
    }

    if (recv_callback_)
    {
        recv_callback_(evt, sender_pid, data, size);
    }
}

void IpcShmChannel::set_send_channel_status(bool connected)
{
    send_channel_connected_.store(connected);
    pkt_que_cv_.notify_one();
    if (send_channel_ && connected) {
        send_channel_->cond.notify_one();
    }
}

void IpcShmChannel::set_recv_channel_status(bool connected)
{
    recv_channel_connected_.store(connected);
    if (recv_channel_) {
        recv_channel_->cond.notify_one();
    }
}

// 构造函数，指定角色和共享内存基础名称
IpcShmChannel::IpcShmChannel(Role role, const std::string &shm_name)
    : role_(role), shm_name_(shm_name), running_(true),  pend_pong_count_(0)
{
    // SERVER: "_up" 是只读（接收），"_down" 是读写（发送）
    // Client: "_up" 是读写（发送），"_down" 是只读（接收）
    write_shm_name_ = (role == Role::CLIENT) ? shm_name + "_UP" : shm_name + "_DOWN";
    read_shm_name_ = (role == Role::CLIENT) ? shm_name + "_DOWN" : shm_name + "_UP";
    if (role == Role::SERVER)
    {
        // 释放共享内存
        shared_memory_object::remove(write_shm_name_.c_str());
        shared_memory_object::remove(read_shm_name_.c_str());
    }
}

// 析构函数，清理资源
IpcShmChannel::~IpcShmChannel()
{
    Stop();
    if (recv_thread_.joinable())
    {
        recv_thread_.join();
    }
    reset_send_channel();
}

bool IpcShmChannel::Start()
{
    // 启动收发线程
    send_thread_ = std::thread(&IpcShmChannel::send_loop, this);

    recv_thread_ = std::thread(&IpcShmChannel::recv_loop, this);

    //启动定时心跳线程
    heart_beat_thread_ = std::thread(&IpcShmChannel::heart_beat_loop,this);

    return true;
}

void IpcShmChannel::Stop()
{
    if (running_)
    {
        bye();
        running_ = false;
        {
            clear_send_queue();          
        }
        try
        {
            if (recv_channel_ != nullptr)
            {
                std::cout << "set recver working false " << std::endl;
                recv_channel_->recver_working = false;
                recv_channel_->cond.notify_all();
            }
        }
        catch (boost::interprocess::interprocess_exception &e)
        {
            if (e.get_error_code() == boost::interprocess::error_code_t::not_such_file_or_directory)
                return;
            throw e;
        }
        send_thread_.join();
        recv_thread_.join();
        heart_beat_thread_.join();
    }
}

// 设置接收回调
void IpcShmChannel::SetRecvCallback(RecvDataCallback cb)
{
    recv_callback_ = cb;
}

bool IpcShmChannel::send(EventType evt, const char *data, size_t size)
{
    DataPacketHeader header{ (short)evt,size,0,1,msg_seq_picker_.fetch_add(1,std::memory_order_seq_cst) };
    return send_packet(evt, &header, data, size);
}

void IpcShmChannel::heart_beat_loop()
{
    while (running_)
    {
        if (send_channel_connected_ && recv_channel_connected_) {
            std::this_thread::sleep_for(std::chrono::seconds(HEART_BEAT_TIME_INTERVAL)); // 每3秒发送一次ping
            if (!ping() || ++pend_pong_count_ > MAX_HEART_BEAT_FAILED_TIME)
            {
                set_recv_channel_status(false);
                set_send_channel_status(false);
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
}

bool IpcShmChannel::post(EventType evt, const char* data, size_t size)
{
    // 若size超过单个包的最大容量，则拆分成若干个不大于DEFULT_BUFFER_CAP的包发送
    DataPacketHeader header{ (short)evt,size,0,1,msg_seq_picker_.fetch_add(1,std::memory_order_seq_cst) };
    if (evt == EventType::msg_data && size > DEFULT_BUFFER_CAP)
    {
        header.chunkTotalCount = (size + DEFULT_BUFFER_CAP - 1) / DEFULT_BUFFER_CAP; // 向上取整

        for (size_t i = 0; i < header.chunkTotalCount; ++i)
        {
            size_t leftSize = size - i * DEFULT_BUFFER_CAP;
            header.chunkSeq = i;
            size_t chunk_size = DEFULT_BUFFER_CAP < leftSize ? DEFULT_BUFFER_CAP : leftSize;
            if (!push_packet(evt, &header, data + i * DEFULT_BUFFER_CAP, chunk_size))
            {
                return false; // 发送失败，后续补充flush消息，避免接收端持续等待
            }
        }
        return true;
    }
    else {
        //直接发送单个包
        return push_packet(evt, &header, data, size);
    }
}

bool IpcShmChannel::push_packet(DataPacketPtr packet)
{
    if (!running_)
        return false;
    std::unique_lock<std::mutex> lk(pkt_que_mtx_);
    packet_que_.push(packet);
    lk.unlock();
    pkt_que_cv_.notify_all();
    return true;
}

DataPacketPtr IpcShmChannel::pop_packet()
{
    DataPacketPtr packet;
    {
        std::unique_lock <std::mutex> lk(pkt_que_mtx_);
        pkt_que_cv_.wait(lk, [this]() { return !send_channel_connected_ || !packet_que_.empty(); });
        if (!send_channel_connected_)
            return nullptr;

        packet = packet_que_.front();
        packet_que_.pop();
    }
    pkt_que_cv_.notify_one();
    return packet;
}

bool IpcShmChannel::push_packet(EventType evt,const DataPacketHeader* header, const char *data, size_t data_size)
{
    return push_packet(DataPacket::make(evt,header, data, data_size));
}

bool IpcShmChannel::send_packet(EventType evt, const DataPacketHeader* header, const char* data, size_t data_size)
{
    if (data_size > DEFULT_BUFFER_CAP || send_channel_ == nullptr)
    {
        std::cout << "send failed, no send channel" << std::endl;
        return false; // 数据大小超
    }
    scoped_lock<interprocess_mutex> lock(send_channel_->mutex);
    bool timeout =  !send_channel_->cond.wait_for(lock,std::chrono::seconds(CHANNEL_TIME_OUT), [this]()
                             { return !send_channel_connected_ || !send_channel_->new_msg ; });
    if (timeout || !send_channel_connected_)
        return false;

    if (!running_ || !send_channel_->recver_working)
    {
        lock.unlock();
        send_channel_->cond.notify_one();
        return false;
    }
    // memset(send_channel_->buffer, 0, DEFULT_BUFFER_CAP);
    header->SetHeader(send_channel_->buffer);
    memcpy(send_channel_->buffer + DataPacketPrefixSize, data, data_size);
    send_channel_->size = data_size + DataPacketPrefixSize;
    send_channel_->new_msg = true;
    send_channel_->ev_type = (short)evt;
    if (evt == EventType::client_byebye || evt == EventType::server_byebye)
        send_channel_->sender_working = false;
    lock.unlock(); // 解锁互斥锁，允许其他线程接收数据
    send_channel_->cond.notify_all();
    return true;
}

void IpcShmChannel::clear_send_queue()
{
    std::unique_lock <std::mutex> lk(pkt_que_mtx_);
    packet_que_.swap(std::queue<DataPacketPtr>());
    pkt_que_cv_.notify_all();
}

void IpcShmChannel::reset_send_channel()
{
    send_channel_ = nullptr;

    // 释放映射区域
    send_region_ = mapped_region();

    // 释放共享内存
    shared_memory_object::remove(write_shm_name_.c_str());

}

bool IpcShmChannel::PostMsg(const char *data, size_t size)
{
    return post(EventType::msg_data, data, size);
}

void IpcShmChannel::recv_loop()
{
    while (running_)
    {
        if (!create_shared_memory(read_shm_name_, recv_shm_, recv_region_, recv_channel_))
        {
            return;
        }

        {
            scoped_lock<interprocess_mutex> lock(recv_channel_->mutex);
            recv_channel_->recver_working = true;
        }
        notify(EventType::start_listen, 0, read_shm_name_.data(), read_shm_name_.size());
        while (recv_channel_connected_)
        {      
            EventType type = EventType::start_listen;
            bool get_completed_msg = false;
            if (!recv(type, get_completed_msg))
                break;
            else if (!get_completed_msg)
                continue;
        }
        notify(EventType::stop_listen, 0, read_shm_name_.data(), read_shm_name_.size());
    }
}

bool IpcShmChannel::recv(EventType& type, bool& get_completed_msg)
{
    scoped_lock<interprocess_mutex> lock(recv_channel_->mutex);
    recv_channel_->cond.wait(lock,
        [this]() {
            return recv_channel_->new_msg
                || !recv_channel_connected_; });

    if (!recv_channel_connected_ || !running_)
    {
        lock.unlock();
        recv_channel_->cond.notify_one();
        return false;
    }
    auto sender_working = recv_channel_->sender_working;
    auto sender_pid = recv_channel_->owner_pid;
    size_t data_size = recv_channel_->size;
    type = (EventType)recv_channel_->ev_type;
    DataPacketHeader header;
    header.GetHeader(recv_channel_->buffer);
    auto cur_msg_seq = header.msgSeq;
    auto msg_len = header.totalLen;
    if (header.chunkSeq == 0 && header.totalLen != 0) {
        //first packet
        auto data_tmp = new char[msg_len];
        recv_msg_map_[header.msgSeq].first = data_tmp;
        recv_msg_map_[header.msgSeq].second = 0;
    }

    char* msg_data = nullptr;
    if (recv_msg_map_.find(cur_msg_seq) != recv_msg_map_.end())
    {
        msg_data = recv_msg_map_[cur_msg_seq].first;
    }
    if (msg_data != nullptr) {
        memcpy(msg_data + header.chunkSeq * DEFULT_BUFFER_CAP, recv_channel_->buffer + DataPacketPrefixSize, data_size - DataPacketPrefixSize);
        recv_msg_map_[cur_msg_seq].second += (data_size - DataPacketPrefixSize);
    }
    recv_channel_->new_msg = false;
    lock.unlock(); // 解锁互斥锁，允许其他线程发送数据
    recv_channel_->cond.notify_one();

    if (header.chunkSeq + 1 < header.chunkTotalCount) {
        return true;
    }
    else if (msg_data != nullptr && recv_msg_map_[cur_msg_seq].second != msg_len) {
        if (recv_msg_map_.find(cur_msg_seq) != recv_msg_map_.end()) {
            delete[]  recv_msg_map_[cur_msg_seq].first;
            recv_msg_map_.erase(cur_msg_seq);
        }
        return true;
    }
    notify(type, sender_pid, msg_data, msg_len);
    if (msg_data != nullptr) {
        delete[] msg_data;
    }

    recv_msg_map_.erase(cur_msg_seq);
    get_completed_msg = true;
    return sender_working;
}

void IpcShmChannel::send_loop()
{
    while (running_) {
        if (!open_shared_memory(write_shm_name_, send_shm_, send_region_, send_channel_))
            break;
        if (send_channel_->recver_working) {
            scoped_lock<interprocess_mutex> lock(send_channel_->mutex);
            send_channel_->sender_working = true;
        }
        else {
            //打开的是一块无主的共享内存，销毁它并重新进入轮询
            send_channel_ = nullptr;
            send_region_ = mapped_region();
            shared_memory_object::remove(write_shm_name_.c_str());
            continue;
        }

        send_channel_connected_.store(true);
        hello();

        while (send_channel_connected_) {
            auto packet = pop_packet();
            if (packet != nullptr) {
                bool ret = send_packet(packet->evt, &(packet->header), packet->data, packet->data_size);
                if (!send_channel_connected_)
                    break;
                else if (!ret)
                {
                    push_packet(packet); //retry
                }
            }
        }

        //reset sendChannel
        std::cout << "reset sendChannel " << std::endl;
        reset_send_channel();
    }
}

bool IpcShmChannel::hello()
{
    std::cout << "send hello" << std::endl;
    return send(role_ == Role::CLIENT ? EventType::client_hello : EventType::server_hello, 0, 0);
}

bool IpcShmChannel::bye()
{
    std::cout << "send bye" << std::endl;
    return send(role_ == Role::CLIENT ? EventType::client_byebye : EventType::server_byebye, 0, 0);
}

bool IpcShmChannel::ping(){
    std::cout << "send ping" << std::endl;
    return  send(IpcShmChannel::EventType::ping, 0, 0);
}

bool IpcShmChannel::pong(){
    std::cout << "send pong" << std::endl;
    return  send(IpcShmChannel::EventType::pong, 0, 0);
}