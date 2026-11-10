/* Typed calls to the running kernel's actual security/MM helpers.
 * Resolve with temporary symbol probes; never replace or disable the checks.
 * Every probe is removed before driver registration. Missing API aborts load.
 */
#ifndef ACE5_PRIVATE_API_H
#define ACE5_PRIVATE_API_H
#include <linux/cache.h>
#include <linux/list_lru.h>
#include <linux/poll.h>
#include <linux/fdtable.h>
#include <linux/capability.h>
#include <linux/sched/mm.h>
#include <linux/security.h>
#include <linux/task_work.h>
#include <linux/mm.h>
int ace5_private_api_init(void);
extern typeof(__list_lru_init) *ace5_api___list_lru_init;
extern typeof(__wake_up_pollfree) *ace5_api___wake_up_pollfree;
extern typeof(close_fd_get_file) *ace5_api_close_fd_get_file;
extern typeof(has_capability_noaudit) *ace5_api_has_capability_noaudit;
extern typeof(list_lru_add) *ace5_api_list_lru_add;
extern typeof(list_lru_count_node) *ace5_api_list_lru_count_node;
extern typeof(list_lru_del) *ace5_api_list_lru_del;
extern typeof(list_lru_destroy) *ace5_api_list_lru_destroy;
extern typeof(list_lru_isolate) *ace5_api_list_lru_isolate;
extern typeof(list_lru_walk_node) *ace5_api_list_lru_walk_node;
extern typeof(mmput_async) *ace5_api_mmput_async;
extern typeof(security_binder_set_context_mgr) *ace5_api_security_binder_set_context_mgr;
extern typeof(security_binder_transaction) *ace5_api_security_binder_transaction;
extern typeof(security_binder_transfer_binder) *ace5_api_security_binder_transfer_binder;
extern typeof(security_binder_transfer_file) *ace5_api_security_binder_transfer_file;
extern typeof(security_cred_getsecid) *ace5_api_security_cred_getsecid;
extern typeof(security_release_secctx) *ace5_api_security_release_secctx;
extern typeof(security_secid_to_secctx) *ace5_api_security_secid_to_secctx;
extern typeof(task_work_add) *ace5_api_task_work_add;
extern typeof(zap_page_range) *ace5_api_zap_page_range;
#ifndef ACE5_PRIVATE_IMPLEMENTATION
#define __list_lru_init (*ace5_api___list_lru_init)
#define __wake_up_pollfree (*ace5_api___wake_up_pollfree)
#define close_fd_get_file (*ace5_api_close_fd_get_file)
#define has_capability_noaudit (*ace5_api_has_capability_noaudit)
#define list_lru_add (*ace5_api_list_lru_add)
#define list_lru_count_node (*ace5_api_list_lru_count_node)
#define list_lru_del (*ace5_api_list_lru_del)
#define list_lru_destroy (*ace5_api_list_lru_destroy)
#define list_lru_isolate (*ace5_api_list_lru_isolate)
#define list_lru_walk_node (*ace5_api_list_lru_walk_node)
#define mmput_async (*ace5_api_mmput_async)
#define security_binder_set_context_mgr (*ace5_api_security_binder_set_context_mgr)
#define security_binder_transaction (*ace5_api_security_binder_transaction)
#define security_binder_transfer_binder (*ace5_api_security_binder_transfer_binder)
#define security_binder_transfer_file (*ace5_api_security_binder_transfer_file)
#define security_cred_getsecid (*ace5_api_security_cred_getsecid)
#define security_release_secctx (*ace5_api_security_release_secctx)
#define security_secid_to_secctx (*ace5_api_security_secid_to_secctx)
#define task_work_add (*ace5_api_task_work_add)
#define zap_page_range (*ace5_api_zap_page_range)
#endif
#endif
