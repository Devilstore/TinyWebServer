// #ifndef LSTTIMER_H
// #define LSTTIMER_H

// #include <stdio.h>
// #include <time.h>

// class http_conn;

// // 定时器节点类： 链表实现
// class ulist_timer
// {
// public:
//     // 节点类构造函数 （无参构造）
//     ulist_timer() : prev(nullptr), next(nullptr), expire(-1),
//                     func(nullptr), user_data(nullptr) {}

// public:
//     time_t expire;             // 到期时间 : 绝对时间
//     void (*func)(http_conn *); // 回调处理函数
//     http_conn *user_data;
//     ulist_timer *prev;
//     ulist_timer *next;
// };

// // 定时器链表  实现。 按到期时间 升序排列
// class timer_list
// {
// public:
//     // 定时器链表构造函数  (构造虚拟头尾节点)
//     timer_list()
//     {
//         head = new ulist_timer();
//         tail = new ulist_timer();
//         head->next = tail;
//         tail->prev = head;
//     }

//     // 定时器链表析构函数
//     ~timer_list()
//     {
//         // 删除当前定时器链表所有节点
//         while (head)
//         {
//             auto t = head;
//             head = head->next;
//             delete t;
//         }
//     }

//     // 添加 定时器节点 到 定时器链表
//     void add_timer(ulist_timer *timer)
//     {
//         if (!timer)
//             return;

//         // 当前 链表 为空
//         if (head->next == tail)
//         {
//             // 添加到 链表头部
//             add_to_head(timer);
//             return;
//         }

//         // 否则插入到头结点以后的链表中
//         add_timer(timer, head);
//     }

//     // 更新当前定时器到正确的位置，只考虑过期时间延长，即往链表当前位置后移动
//     void update_timer(ulist_timer *timer)
//     {
//         if (!timer)
//             return;

//         // 如果当前为最后一个有效定时器节点 或者 更新后的过期时间仍然比后继节点的过期时间小
//         if (timer->next == tail || timer->expire < timer->next->expire)
//         {
//             return; // 无需更新
//         }

//         remove_node(timer);            // 摘除 timer
//         add_timer(timer, timer->prev); // 重新在 timer->prev 之后插入合适的位置
//     }

//     void del_timer(ulist_timer *timer)
//     {
//         if (!timer)
//             return;

//         remove_node(timer); // 直接 摘除 timer
//         delete timer;       // 释放内存
//     }

//     // SIGALARM 信号每次被触发，就在其信号处理函数中 调用一次 tick() 函数。
//     // 用以处理到期的链表任务
//     void tick()
//     {
//         // 链表空
//         if (head->next == tail)
//             return;

//         printf("调用 tick()... \n");
//         time_t curtime = time(NULL);   // 获取当前系统时间
//         ulist_timer *cur = head->next; // 从 有效的开始节点进行扫描处理
//         char ip[16];
//         cur->user_data->getClientIp(ip);
//         printf("data:%s\n", ip);
//         printf("cur->expire : %ld \n", cur->expire);
//         // 遍历到尾节点
//         while (cur != tail)
//         {
//             // if (!cur || !cur->expire)
//             // {
//             //     printf("有问题...\n");
//             //     break;
//             // }
//             // 最小到期时间 尚未到达， 直接返回
//             if (curtime < cur->expire)
//             {
//                 printf("。。。tick2()... \n");
//                 break;
//             }
//             printf("。。。tick3()... \n");
//             ulist_timer *t = cur->next; // 临时保存后继
//             cur->func(cur->user_data);  // 调用回调函数  执行任务对象到期处理函数
//             cur = t;                    // 继续遍历后继节点
//         }
//     }

// private:
//     // 重载的辅助函数 将 timer 加入到 node 之后的链表中
//     void add_timer(ulist_timer *timer, ulist_timer *node)
//     {
//         if (!timer)
//             return;
//         ulist_timer *cur = node->next; // 记录后继 进行插入
//         while (cur != tail)
//         {
//             if (timer->expire < cur->expire)
//             {
//                 add_one(timer, cur); // timer 添加到 cur 之前
//                 break;
//             }
//             cur = cur->next;
//         }

//         // 遍历到尾部 也没有插入，则直接插入尾部。当前过期时间 在链表中是最大的
//         if (cur == tail)
//         {
//             add_one(timer, cur);
//         }
//     }

//     // 将 timer 加入到 node 前面
//     void add_one(ulist_timer *timer, ulist_timer *node)
//     {
//         if (!timer)
//             return;
//         timer->next = node;
//         timer->prev = node->prev;
//         node->prev->next = timer;
//         node->prev = timer;
//     }

//     // 添加 timer 到 链表头部
//     void add_to_head(ulist_timer *timer)
//     {
//         if (!timer)
//             return;
//         timer->next = head->next;
//         timer->prev = head;
//         head->next->prev = timer;
//         head->next = timer;
//     }

//     // 将 timer 中 链表中摘除, 并返回 timer
//     ulist_timer *remove_node(ulist_timer *timer)
//     {
//         if (timer)
//         {
//             timer->next->prev = timer->prev;
//             timer->prev->next = timer->next;
//             timer->next = nullptr;
//             timer->prev = nullptr;
//         }
//         return timer;
//     }

// private:
//     ulist_timer *head;
//     ulist_timer *tail;
// };

// #endif