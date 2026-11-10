/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ACE5_TOKENS_H
#define ACE5_TOKENS_H
#include <linux/types.h>
struct task_struct;
u64 ace5_task_token(struct task_struct *task, bool first);
int ace5_set_task_token(struct task_struct *task, u64 value, bool first);
int ace5_token_device_init(void);
void ace5_token_device_exit(void);
#endif
