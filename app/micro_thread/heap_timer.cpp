/**
 * @file  heap_timer.cpp
 */

#include "heap_timer.h"
#include "micro_thread.h"

using namespace NS_MICRO_THREAD;


/**
 * @brief 构造函数
 */
CTimerMng::CTimerMng(uint32_t max_item)
{
    #define TIMER_MIN 100000

    if (max_item < TIMER_MIN)
    {
        max_item = TIMER_MIN;
    }

    _heap = new HeapList(max_item);
}


/**
 * @brief 析构函数
 */
CTimerMng::~CTimerMng()
{
    if (_heap) {
        delete _heap;
        _heap = NULL;
    }
}


/**
 * @brief 定时器设置函数
 * @param timerable 定时器对象
 * @param interval  超时的间隔 ms单位
 * @return 成功返回true, 否则失败
 */
bool CTimerMng::start_timer(CTimerNotify* timerable, uint32_t interval)
{
    if (!_heap || !timerable) {
        return false;
    }

    utime64_t now_ms = MtFrame::Instance()->GetLastClock();
    timerable->set_expired_time(now_ms + interval);
    int32_t ret = _heap->HeapPush(timerable);
    if (ret < 0) {
        MTLOG_ERROR("timer start failed(%p), ret(%d)", timerable, ret);
        return false;
    }

    return true;
}

/**
 * @brief 定时器停止接口函数
 * @param timerable 定时器对象
 */
void CTimerMng::stop_timer(CTimerNotify* timerable)
{
    if (!_heap || !timerable) {
        return;
    }
    
    _heap->HeapDelete(timerable);
    return;
}

/**
 * @brief 定时器超时检测函数
 */
void CTimerMng::check_expired() 
{
    if (!_heap) {
        return;
    }
    
    utime64_t now = MtFrame::Instance()->GetLastClock();
    CTimerNotify* timer = dynamic_cast<CTimerNotify*>(_heap->HeapTop());
    while (timer && (timer->get_expired_time() <= now))
    {
        _heap->HeapDelete(timer);
        timer->timer_notify();
        timer = dynamic_cast<CTimerNotify*>(_heap->HeapTop());
    }    
};



