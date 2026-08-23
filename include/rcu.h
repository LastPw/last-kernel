#ifndef RCU_H
#define RCU_H

#include "types.h"

typedef struct rcu_head {
    struct rcu_head* next;
    void (*func)(struct rcu_head* head);
} rcu_head_t;

void rcu_init(void);
void rcu_read_lock(void);
void rcu_read_unlock(void);
void synchronize_rcu(void);
void call_rcu(rcu_head_t* head, void (*func)(rcu_head_t* head));
u64 rcu_batch_completed(void);

#endif /* RCU_H */
