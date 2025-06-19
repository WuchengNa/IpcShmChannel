# IpcShmChannel

基于Boost.Interprocess的进程间通信(IPC)共享内存通道实现。支持文本、JSON和Protobuf消息传输。

## 特性

- 支持服务端/客户端架构
- 双向通信通道
- 多种消息类型支持:
  - 文本消息
  - JSON消息 
  - Protobuf消息
- 基于共享内存的高性能通信
- 线程安全设计
- 异步消息回调机制

## 依赖

- C++11 及以上
- Boost库 (Interprocess组件)
- CMake 3.15及以上 

## 编译

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## 用法

1. 引入头文件:

```cpp
#include "IpcShmChannel.h"
```

2. 创建通道实例:

```cpp
// 服务端
IpcShmChannel server(IpcShmChannel::Role::SERVER, "MyChannel");

// 客户端
IpcShmChannel client(IpcShmChannel::Role::CLIENT, "MyChannel"); 
```

3. 设置消息回调:

```cpp
channel.SetRecvCallback([](IpcShmChannel::EventType evt, int sender_pid, const char* data, size_t size) {
    // 处理接收到的消息
});
```

4. 启动通道:

```cpp
channel.Start();
```

5. 发送消息:

```cpp
// 发送文本消息
channel.SendTextMsg("Hello", 5);

// 发送JSON消息
channel.SendJsonMsg(json_str.c_str(), json_str.size());

// 发送Protobuf消息 
channel.SendPbMsg(pb_data, pb_size);
```

6. 关闭通道:

```cpp
channel.Stop();
```

## 测试

项目包含完整的单元测试和压力测试用例。运行测试:

```bash
cd build
./IpcShmChannel
```

## 许可

本项目采用 MIT 许可证。

## 作者

wucheng