# pthread_* API

## 信号量

```c
sem_t s;
sem_init(&s, 0, 要初始化的值);
sem_wait(&s);    // P操作 (--，如果<0则pending(sleeping))
sem_post(&s);    // V操作 (++，如果<=0则wakeup)
```

## mutex

```c
pthread_mutex_t m;
pthread_mutex_init(&m, NULL);
pthread_mutex_lock(&m);
pthread_mutex_unlock(&m);
```

## pthread

```c
pthread_t m;
pthread_create(&m, NULL, func, NULL【参数指针】);
pthread_join(&m, NULL【返回值接收指针】);
pthread_exit(NULL【返回值接收指针】);     // 调用者线程被关闭
```