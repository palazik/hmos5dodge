// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <grp.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <linux/android/binder.h>
#include <linux/android/binderfs.h>
#include "access_tokenid.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s errno=%d (%s)\n",__LINE__,#x,errno,strerror(errno)); exit(1); } } while (0)
static int token_fd;
static const uint64_t CLIENT = 0x28012345, FIRST = 0x28054321, SERVER = 0x28067890;
static uint64_t get_token(bool first)
{
    uint64_t value = 0;
    REQUIRE(ioctl(token_fd, first ? ACCESS_TOKENID_GET_FTOKENID : ACCESS_TOKENID_GET_TOKENID, &value) == 0);
    return value;
}
static void set_token(uint64_t value, bool first)
{
    REQUIRE(ioctl(token_fd, first ? ACCESS_TOKENID_SET_FTOKENID : ACCESS_TOKENID_SET_TOKENID, &value) == 0);
}
static void wait_ok(pid_t pid)
{
    int status;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void *thread_test(void *unused)
{
    REQUIRE(get_token(false) == CLIENT);
    REQUIRE(get_token(true) == 0);
    set_token(SERVER, false);
    REQUIRE(get_token(false) == SERVER);
    return NULL;
}
struct token_result { uint64_t sender, first, reserved[2]; };
#define FEATURE_SET _IOWR('b', 30, uint64_t)
#define GET_ACCESS_TOKEN _IOWR('b', 31, struct token_result)

static void test_binder(const char *directory)
{
    char path[512];
    bool direct = strncmp(directory,"/dev/",5) == 0;
    if (direct) {
        snprintf(path,sizeof(path),"%s",directory);
    } else {
    snprintf(path,sizeof(path),"%s/binder-control",directory);
    int ctl = open(path,O_RDWR|O_CLOEXEC);
    REQUIRE(ctl >= 0);
    struct binderfs_device device = { .name="ace5-token-test" };
    REQUIRE(ioctl(ctl,BINDER_CTL_ADD,&device) == 0);
    close(ctl);
    snprintf(path,sizeof(path),"%s/ace5-token-test",directory);
    }
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);
    pid_t server = fork();
    REQUIRE(server >= 0);
    if (!server) {
        alarm(8);
        close(pipefd[0]);
        int fd = open(path,O_RDWR|O_CLOEXEC);
        REQUIRE(fd >= 0);
        void *map = mmap(NULL,1024*1024,PROT_READ,MAP_PRIVATE,fd,0);
        REQUIRE(map != MAP_FAILED);
        uint64_t features = 0;
        REQUIRE(ioctl(fd,FEATURE_SET,&features) == 0 && features == 1);
        REQUIRE(ioctl(fd,FEATURE_SET,(void *)1) == -1 && errno == EFAULT);
        uint64_t *lazy = mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        REQUIRE(lazy != MAP_FAILED);
        REQUIRE(ioctl(fd,FEATURE_SET,lazy) == 0 && *lazy == 1);
        REQUIRE(munmap(lazy,4096) == 0);
        REQUIRE(ioctl(fd,_IOWR('b',125,uint64_t),&features) == -1 && errno == EINVAL);
        struct token_result initial = { .sender=~0ULL, .first=~0ULL };
        REQUIRE(ioctl(fd,GET_ACCESS_TOKEN,&initial) == 0 && initial.sender == 0 && initial.first == 0);
        puts("PASS Binder rejects invalid pointers/commands and handles demand-paged user buffers");
        REQUIRE(ioctl(fd,BINDER_SET_CONTEXT_MGR,0) == 0);
        set_token(SERVER,false);
        REQUIRE(write(pipefd[1],"R",1) == 1);
        close(pipefd[1]);
        uint32_t enter = BC_ENTER_LOOPER;
        unsigned char readbuf[4096];
        struct binder_write_read bwr = { .write_size=sizeof(enter), .write_buffer=(uintptr_t)&enter };
        REQUIRE(ioctl(fd,BINDER_WRITE_READ,&bwr) == 0);
        for (;;) {
            memset(&bwr,0,sizeof(bwr));
            bwr.read_size=sizeof(readbuf); bwr.read_buffer=(uintptr_t)readbuf;
            REQUIRE(ioctl(fd,BINDER_WRITE_READ,&bwr) == 0);
            size_t offset=0;
            while (offset+4 <= bwr.read_consumed) {
                uint32_t cmd; memcpy(&cmd,readbuf+offset,4); offset+=4;
                if (cmd == BR_TRANSACTION || cmd == BR_TRANSACTION_SEC_CTX) {
                    struct token_result tokens={0};
                    REQUIRE(ioctl(fd,GET_ACCESS_TOKEN,&tokens) == 0);
                    REQUIRE(tokens.sender == CLIENT && tokens.first == FIRST);
                    REQUIRE(get_token(false) == SERVER);
                    puts("PASS Binder preserves sender token and first caller; receiver self token remains distinct");
                    fflush(stdout); _exit(0);
                }
                offset += _IOC_SIZE(cmd);
            }
        }
    }
    close(pipefd[1]); char ready;
    REQUIRE(read(pipefd[0],&ready,1) == 1 && ready == 'R'); close(pipefd[0]);
    int fd=open(path,O_RDWR|O_CLOEXEC); REQUIRE(fd >= 0);
    REQUIRE(mmap(NULL,1024*1024,PROT_READ,MAP_PRIVATE,fd,0) != MAP_FAILED);
    unsigned char buffer[4+sizeof(struct binder_transaction_data)];
    uint32_t cmd=BC_TRANSACTION, data=0xace50006;
    struct binder_transaction_data tr={0};
    tr.target.handle=0;tr.code=0xace5;tr.flags=TF_ONE_WAY;
    tr.data_size=sizeof(data);tr.data.ptr.buffer=(uintptr_t)&data;
    memcpy(buffer,&cmd,4);memcpy(buffer+4,&tr,sizeof(tr));
    struct binder_write_read bwr={ .write_size=sizeof(buffer),.write_buffer=(uintptr_t)buffer };
    REQUIRE(ioctl(fd,BINDER_WRITE_READ,&bwr) == 0);
    wait_ok(server);close(fd);if (!direct) unlink(path);
}
int main(int argc,char **argv)
{
    REQUIRE(argc == 3);
    setbuf(stdout,NULL);alarm(20);
    token_fd=open(argv[1],O_RDWR|O_CLOEXEC);REQUIRE(token_fd >= 0);
    set_token(CLIENT,false);set_token(FIRST,true);
    REQUIRE(get_token(false)==CLIENT && get_token(true)==FIRST);
    pid_t p=fork();REQUIRE(p>=0);
    if (!p) { REQUIRE(get_token(false)==CLIENT && get_token(true)==0);_exit(0); }
    wait_ok(p);puts("PASS token read/write and fork inheritance, first-caller reset");
    pthread_t thread;REQUIRE(pthread_create(&thread,NULL,thread_test,NULL)==0);
    REQUIRE(pthread_join(thread,NULL)==0);
    REQUIRE(get_token(false)==CLIENT && get_token(true)==FIRST);
    puts("PASS thread inheritance and per-task isolation");
    ioctl_add_perm_data perms={.token=0x12345};
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_ADD_PERMISSIONS,&perms)==0);
    ioctl_set_get_perm_data permission={.token=perms.token,.op_code=37,.is_granted=true};
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&permission)==0);
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&permission)==0);
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&permission)==1);
    permission.is_granted=false;
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&permission)==0);
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&permission)==0);
    p=fork();REQUIRE(p>=0);
    if (!p) {
        REQUIRE(setgroups(0,NULL)==0 && setgid(10000)==0 && setuid(10000)==0);
        uint64_t bad=SERVER;
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_TOKENID,&bad)==-1 && errno==EPERM);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_FTOKENID,&bad)==-1 && errno==EPERM);
        permission.is_granted=true;
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&permission)==-1 && errno==EPERM);
        REQUIRE(get_token(false)==CLIENT);
        _exit(0);
    }
    wait_ok(p);
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_REMOVE_PERMISSIONS,&perms.token)==0);
    REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&permission)==-1 && errno==ENODATA);
    puts("PASS grant, revoke, remove, and unauthorized mutation rejection");
    p=fork();REQUIRE(p>=0);
    if (!p) {
        gid_t group=3020;
        REQUIRE(setgroups(1,&group)==0 && setgid(3020)==0 && setuid(3020)==0);
        set_token(SERVER,false);set_token(FIRST,true);
        REQUIRE(get_token(false)==SERVER && get_token(true)==FIRST);
        ioctl_add_perm_data owned={.token=0x12346};
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_ADD_PERMISSIONS,&owned)==0);
        ioctl_set_get_perm_data perm={.token=owned.token,.op_code=2047,.is_granted=true};
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&perm)==0);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&perm)==1);
        perm.is_granted=false;REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&perm)==0);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_GET_PERMISSION,&perm)==0);
        perm.op_code=2048;REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_PERMISSION,&perm)==-1 && errno==EINVAL);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_REMOVE_PERMISSIONS,&owned.token)==0);
        _exit(0);
    }
    wait_ok(p);puts("PASS real AccessToken service UID/GID 3020 authority and permission-bit bounds");
    p=fork();REQUIRE(p>=0);
    if (!p) {
        REQUIRE(setgroups(0,NULL)==0 && setgid(3081)==0 && setuid(3081)==0);
        uint64_t ordinary=SERVER,render=SERVER|(1ULL<<25);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_TOKENID,&ordinary)==-1 && errno==EPERM);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_TOKENID,&render)==0);
        REQUIRE(get_token(false)==render);
        REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_FTOKENID,&FIRST)==-1 && errno==EPERM);
        _exit(0);
    }
    wait_ok(p);puts("PASS nwebspawn UID 3081 render-token restriction; ordinary-token and first-caller spoof rejected");
    test_binder(argv[2]);
    close(token_fd);puts("PASS kernel AccessToken contract tests");return 0;
}
