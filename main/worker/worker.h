#ifndef WORKER_H
#define WORKER_H

#include <stdint.h>

typedef void (*worker_task_cb)(void *arg);

int worker_init(void);
void worker_task(void *param);
int worker_queue_push_task(worker_task_cb cb, void *arg);

#endif  // WORKER_H
