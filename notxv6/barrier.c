#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
    pthread_mutex_t barrier_mutex;
    pthread_cond_t barrier_cond;
    int nthread;  // Number of threads that have reached this round of the barrier
    int round;    // Barrier round
} bstate;

static void barrier_init(void) {
    assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
    assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
    bstate.nthread = 0;
}

static void barrier() {

    // 上锁防止被插入
    pthread_mutex_lock(&bstate.barrier_mutex);

    bstate.nthread++;
    // 当此轮全部遍历完 唤醒所有线程 进入下一轮并且初始化状态
    if (bstate.nthread == nthread) {
        bstate.nthread = 0;
        bstate.round++;
        pthread_cond_broadcast(&bstate.barrier_cond);
    } else {
        // 这个线程完事了 但是没遍历完 所以此时需要等待其他线程
        // 放下书中的锁 把资源给别的人用 （虽然这里没的资源）
        // 注意 调用wait的时候 手中必须持有锁
        // 所以这里的分支就可以 怎么都能放下所
        pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
    }

    pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void* thread(void* xa) {
    long n = (long)xa;
    long delay;
    int i;

    for (i = 0; i < 20000; i++) {
        int t = bstate.round;
        assert(i == t);
        barrier();
        usleep(random() % 100);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    pthread_t* tha;
    void* value;
    long i;
    double t1, t0;

    if (argc < 2) {
        fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
        exit(-1);
    }
    nthread = atoi(argv[1]);
    tha = malloc(sizeof(pthread_t) * nthread);
    srandom(0);

    barrier_init();

    for (i = 0; i < nthread; i++) {
        assert(pthread_create(&tha[i], NULL, thread, (void*)i) == 0);
    }
    for (i = 0; i < nthread; i++) {
        assert(pthread_join(tha[i], &value) == 0);
    }
    printf("OK; passed\n");
}
