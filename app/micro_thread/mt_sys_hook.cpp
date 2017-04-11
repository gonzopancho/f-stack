/**
 *  @filename mt_sys_hook.cpp
 *  @info  微线程hook系统api, 以不用额外编译的优势, 转同步为异步库
 *         只hook socket相关的API, HOOK 部分, 参考pth与libco实现
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "micro_thread.h"
#include "mt_sys_hook.h"
#include "ff_hook.h"

using namespace NS_MICRO_THREAD;


#define MT_HOOK_MAX_FD      65535*2     
#define MT_FD_FLG_INUSE     0x1
#define MT_FD_FLG_UNBLOCK   0x2


/**
 * @brief 每sockt分配一个管理结构, 标记是否需要HOOK, 超时时间等
 */
typedef struct socket_hook_info
{
    int     sock_flag;      // 是否使用HOOK, 是否用户设置UNBLOCK
    int     read_timeout;   // socket读取超时时间, ms单位
    int     write_timeout;  // socket写入超时时间, ms单位
}MtHookFd;

MtSyscallFuncTab       g_mt_syscall_tab;            // 全局符号表
int                    g_mt_hook_flag;              // 全局控制标记
int                    g_ff_hook_flag;              // 全局控制标记
static MtHookFd        g_mt_hook_fd_tab[MT_HOOK_MAX_FD];   // 全局fd管理

/**
 * @brief 内部接口, 获取hook fd相关的信息, socket 默认hook, open 默认no hook
 */
MtHookFd* mt_hook_find_fd(int fd) 
{
    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD)) {
        return NULL;
    }  

    MtHookFd* fd_info =  &g_mt_hook_fd_tab[fd];
    if (!(fd_info->sock_flag & MT_FD_FLG_INUSE)) {
        return NULL;
    } else {
        return fd_info;
    }
}

/**
 * @brief 内部接口, 创建socket设置hook, 只处理socket, 不处理文件IO
 */
void mt_hook_new_fd(int fd)
{
    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD)) {
        return;
    }  

    MtHookFd* fd_info       = &g_mt_hook_fd_tab[fd];
    fd_info->sock_flag      = MT_FD_FLG_INUSE;
    fd_info->read_timeout   = 500;
    fd_info->write_timeout  = 500;
}

/**
 * @brief 内部接口, 关闭hook socket
 */
void mt_hook_free_fd(int fd)
{
    if ((fd < 0) || (fd >= MT_HOOK_MAX_FD)) {
        return;
    }  

    MtHookFd* fd_info       = &g_mt_hook_fd_tab[fd];
    fd_info->sock_flag      = 0;
    fd_info->read_timeout   = 0;
    fd_info->write_timeout  = 0;
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 截获用户设置的非阻塞标记
 */
#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int fd, unsigned long cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    va_end(ap);

    mt_hook_syscall(ioctl);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_ioctl(fd, cmd, arg);
    }

    if (cmd == FIONBIO)
    {
        int flags =  (arg != NULL) ? *((int*)arg) : 0;
        if (flags != 0) {
            hook_fd->sock_flag |= MT_FD_FLG_UNBLOCK;
        }
    }

	return ff_hook_ioctl(fd, cmd, arg);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 默认设置unblock
 */
int socket(int domain, int type, int protocol)
{
    mt_hook_syscall(socket);
	
    if (!mt_hook_active())	
    {
       return ff_hook_socket(domain, type, protocol);
    }

    int fd = ff_hook_socket(domain, type, protocol);
    if (fd < 0)
    {
        return fd;
    }

    mt_hook_new_fd(fd);


    mt_hook_syscall(ioctl);
	int nb = 1;
	ff_hook_ioctl(fd, FIONBIO, &nb);

    return fd;
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api
 */
int close(int fd)
{
    mt_hook_syscall(close);
    if (!mt_hook_active())	
    {
        return ff_hook_close(fd);
    }

    mt_hook_free_fd(fd);
	return ff_hook_close(fd);
}


/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api
 */
int connect(int fd, const struct sockaddr *address, socklen_t address_len)
{
    mt_hook_syscall(connect);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)	
    {
        return ff_hook_connect(fd, address, address_len);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_connect(fd, address, address_len);
    }

    return MtFrame::connect(fd, address, (int)address_len, hook_fd->write_timeout);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t read(int fd, void *buf, size_t nbyte)
{
    mt_hook_syscall(read);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_read(fd, buf, nbyte);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_read(fd, buf, nbyte);
    }
    
    return MtFrame::read(fd, buf, nbyte, hook_fd->read_timeout);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t write(int fd, const void *buf, size_t nbyte)
{
    mt_hook_syscall(write);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_write(fd, buf, nbyte);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_write(fd, buf, nbyte);
    }
    
    return MtFrame::write(fd, buf, nbyte, hook_fd->write_timeout);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t sendto(int fd, const void *message, size_t length, int flags, 
               const struct sockaddr *dest_addr, socklen_t dest_len)
{
    mt_hook_syscall(sendto);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_sendto(fd, message, length, flags, dest_addr, dest_len);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_sendto(fd, message, length, flags, dest_addr, dest_len);
    }

    return MtFrame::sendto(fd, message, (int)length, flags, 
                           dest_addr, dest_len, hook_fd->write_timeout);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t recvfrom(int fd, void *buffer, size_t length, int flags, 
                  struct sockaddr *address, socklen_t *address_len)
{
    mt_hook_syscall(recvfrom);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_recvfrom(fd, buffer, length, flags, address, address_len);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_recvfrom(fd, buffer, length, flags, address, address_len);
    }
    
    return MtFrame::recvfrom(fd, buffer, length, flags, address, address_len, hook_fd->read_timeout);

}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t recv(int fd, void *buffer, size_t length, int flags)
{
    mt_hook_syscall(recv);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_recv(fd, buffer, length, flags);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_recv(fd, buffer, length, flags);
    }
    
    return MtFrame::recv(fd, buffer, length, flags, hook_fd->read_timeout);
}

/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 用户已经设置unblock, 跳过
 */
ssize_t send(int fd, const void *buf, size_t nbyte, int flags)
{
    mt_hook_syscall(send);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_send(fd, buf, nbyte, flags);
    }

    if (hook_fd->sock_flag & MT_FD_FLG_UNBLOCK) 
    {
        return ff_hook_send(fd, buf, nbyte, flags);
    }
    
    return MtFrame::send(fd, buf, nbyte, flags, hook_fd->write_timeout);
}


/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 截获用户设置的超时时间信息
 */
int setsockopt(int fd, int level, int option_name, const void *option_value, socklen_t option_len)
{
    mt_hook_syscall(setsockopt);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_setsockopt(fd, level, option_name, option_value, option_len);
    }

    if (SOL_SOCKET == level)
    {
        struct timeval *val = (struct timeval*)option_value;
        if (SO_RCVTIMEO == option_name) 
        {
            hook_fd->read_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
        else if (SO_SNDTIMEO == option_name)
        {
            hook_fd->write_timeout = val->tv_sec * 1000 + val->tv_usec / 1000;
        }
    }

	return ff_hook_setsockopt(fd, level, option_name, option_value, option_len);
}



/**
 * @brief HOOK接口, 初始化mt库后, 接管系统api, 截获用户设置的非阻塞标记
 */
int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void* arg = va_arg(ap, void *);
    va_end(ap);

    mt_hook_syscall(fcntl);
    MtHookFd* hook_fd = mt_hook_find_fd(fd); 
    if (!mt_hook_active() || !hook_fd)
    {
        return ff_hook_fcntl(fd, cmd, arg);
    }

    if (cmd == F_SETFL)
    {
        va_start(ap, cmd);
        int flags = va_arg(ap, int);
        va_end(ap);
        
        if (flags & O_NONBLOCK) 
        {
            hook_fd->sock_flag |= MT_FD_FLG_UNBLOCK;
        }
    }

    return ff_hook_fcntl(fd, cmd, arg);
}


int listen(int sockfd, int backlog)
{
    mt_hook_syscall(listen);
	return ff_hook_listen(sockfd, backlog);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    mt_hook_syscall(bind);
	return ff_hook_bind(sockfd, addr, addrlen);
}

int accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
    mt_hook_syscall(accept);
	return ff_hook_accept(fd, addr, addrlen);
}

#ifdef __cplusplus
}
#endif
