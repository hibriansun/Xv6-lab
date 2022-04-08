

// 1. 使用信号量实现互斥访问
// 信号量初始值设为1，表示空余"一张门票"
// 后续信号量状态：
// 如果互斥信号量为 1，表示没有线程进⼊临界区；
// 如果互斥信号量为 0，表示有⼀个线程进⼊临界区；
// 如果互斥信号量为 -1，表示⼀个线程进⼊临界区，另⼀个线程等待进⼊。

// #include "kernel/semaphore.h"
// #include "kernel/defs.h"


// 2. 同步 -- 生产者消费者
// 2.1      1 v.s. 1
//     厨师等待做菜P(S1)，顾客饥饿，发出做菜请求V(S1)，顾客开始始pending P(S2)做菜结束，厨师pending S1结束，开始做菜
//     做菜完毕后，发出V(S2)请求，顾客pending S2结束
//     **** 一个信号量相当于一件等待事件，一个等待队列，p->chan(type: void*)对应不同两个信号量中也能表明 ****
//     Q: 为什么不使用同一个信号量完成 (等待、请求)做菜 和 (等待、请求)上菜 两件事？
//     A: 如果可以的话，顾客刚发出做菜请求紧接着自己和厨师都在抢接到 做菜请求 的信息，最后就看谁快谁抢到，如果厨师没抢到就造成了deadlock


// 2.2      N v.s. M
//     *****既需要互斥又需要同步*****
//     原则：任何时刻， 只能有⼀个⽣产者或消费者可以访问缓冲区(shared)
//     原料：
//      1. 互斥信号量：保证共享缓冲区同时只有一个人
//      2. 资源信号量(缓冲区资源数)：⽤于消费者询问缓冲区是否有数据
//      3. 资源信号量(缓冲区空位数)：⽤于⽣产者询问缓冲区是否有空位
//      4. buffer数组
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#define PRODUCEBUFSZ    5
#define Popt(sem)   sem_wait(&sem);
#define Vopt(sem)   sem_post(&sem);

sem_t empty;
sem_t full;
pthread_mutex_t t;

int buffCapacity = 0;

void produce2buff() {
    buffCapacity++;
    printf("buffCapicity: %d from the producer\n", buffCapacity);
}

void consumebuff() {
    buffCapacity--;
    printf("buffCapicity: %d from the consumer\n", buffCapacity);
}

void producerConsumerInit() {
    sem_init(&empty, 0, PRODUCEBUFSZ);
    sem_init(&full, 0, 0);
    pthread_mutex_init(&t, NULL);
}

void producer() {
    while (1) {
        Popt(empty);

        pthread_mutex_lock(&t);
        produce2buff();
        pthread_mutex_unlock(&t);

        Vopt(full);     // ++
                        // if full <= 0
                        //  wakeup(full)
    }
}

void consumer() {
    while (1) {

        Popt(full);     // --
                        // while full < 0
                        //    sleep
    
        pthread_mutex_lock(&t);
        consumebuff();
        pthread_mutex_unlock(&t);
    
        Vopt(empty);
    }
}

void producerConsumer() {
    pthread_t thread[2];

    producerConsumerInit();
    pthread_create(&thread[0], NULL, producer, NULL);
    pthread_create(&thread[1], NULL, consumer, NULL);

    pthread_join(&thread[0], NULL);
    pthread_join(&thread[1], NULL);
}


// 3. 同步 -- 哲学家就餐
//  



// 4. 同步 -- 读写-写者问题


int main()
{
    producerConsumer();
    
}