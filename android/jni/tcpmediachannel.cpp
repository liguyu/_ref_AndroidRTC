#include <assert.h>
#include <iostream>
#include "talk/base/socketstream.h"
#include "talk/base/asyncsocket.h"
#include "tcpmediachannel.h"
#include "helper.h"

#ifdef JNI_BASE
#include <android/log.h>
#define  LOG_TAG    "MVRS"
#define  LOGD(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)  
#else
#define LOGD(...)  {}
#endif


TcpMediaChannel::TcpMediaChannel() {
    tcp_socket_ = NULL;
}

TcpMediaChannel::~TcpMediaChannel() {
    Close();
}


bool TcpMediaChannel::Close(){
    if( tcp_socket_) {
        tcp_socket_->SignalCloseEvent.disconnect(this); 
        tcp_socket_->SignalConnectEvent.disconnect(this); 
        tcp_socket_->Close();
        delete tcp_socket_; 
        tcp_socket_ = NULL;
    }
    return true;
}

void TcpMediaChannel::createSocket() {
    talk_base::Thread* pth = talk_base::Thread::Current();
    talk_base::AsyncSocket* socket =
        pth->socketserver()->CreateAsyncSocket(SOCK_STREAM);
    tcp_socket_ = socket; 
    assert(tcp_socket_);
    tcp_socket_->SignalConnectEvent.connect(this, &TcpMediaChannel::OnConnectEvent);
    tcp_socket_->SignalCloseEvent.connect(this,   &TcpMediaChannel::OnCloseEvent);
    tcp_socket_->SignalReadEvent.connect(this, &TcpMediaChannel::OnReadEvent);
}

int TcpMediaChannel::doConnect() {
    MediaURL murl;
    int port;
    std::string remote_addr;
    if ( murl.Parse(my_url_) ) {
        remote_addr = murl.location;
        port = atoi( murl.resource.c_str());
    } else {
        return -1;
    }  

    talk_base::SocketAddress addr(remote_addr, port);
    LOGD( addr.ToString().c_str());
    if (tcp_socket_->Connect(addr) < 0) {
        if ( tcp_socket_->IsBlocking() ) {
            return 0;
        }
        return -1;
    }
    return 1;
}

bool TcpMediaChannel::Connect(const std::string &url) {
    my_url_ = url;
    is_ready_ = false;
    Close();

    createSocket();
    int ret = doConnect();

    if ( ret < 0) {
        Close();    
        SignalChannelOpened(this, false); 
        return true;
    }

    return false;
}

int TcpMediaChannel::PushData(const unsigned char *data, size_t len) {
    if ( tcp_socket_ == NULL)
        return -1;

    if ( tcp_socket_->GetState() == talk_base::Socket::CS_CONNECTING)
        return 0;

    int result = tcp_socket_->Send(data, len);
    if (result < 0) {
        if (tcp_socket_->IsBlocking())
            return 0;
        OnCloseEvent(tcp_socket_, errno);
        return -1;
    }

    return result;
}

void TcpMediaChannel::OnConnectEvent(talk_base::AsyncSocket* socket) {
    assert(socket == tcp_socket_);
    LOGD("Detecting OnConnectEvent!\n");
    if ( tcp_socket_->GetState() == talk_base::Socket::CS_CONNECTED) {
        if ( is_ready_ == false) {
            is_ready_ = true;
            SignalChannelOpened(this, true);
            return;
        }
    }

    if ( tcp_socket_->GetState() == talk_base::Socket::CS_CLOSED) {
        if ( is_ready_ == false) {
            Close();
            SignalChannelOpened(this, false);
        }else {
            is_ready_ = false;
            Close();
            SignalChannelClosed(this);
        }
    }
} 


void TcpMediaChannel::OnReadEvent(talk_base::AsyncSocket* socket) {
    assert(socket == tcp_socket_);

    unsigned char temp[2048];
    int ret = sock_->Recv(temp, sizeof(temp) - 1);
    if (ret > 0) {
        SignalDataRead(this, temp, ret);
    }   
}

void TcpMediaChannel::OnCloseEvent(talk_base::AsyncSocket* socket, int err) {
    assert(socket == tcp_socket_);
    if ( is_ready_ == false) {
        //SignalChannelClosed(this);
        return;
    }

    Close();

    createSocket();
    int ret = doConnect();

    if ( ret < 0) {
        is_ready_ = false;
        Close();
        SignalChannelClosed(this);
    }
}
