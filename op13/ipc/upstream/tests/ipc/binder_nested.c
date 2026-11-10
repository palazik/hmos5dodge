// SPDX-License-Identifier: GPL-2.0
/* Exercise real synchronous calls, nested callbacks and concurrent identities. */
#define main basic_contract_main
#include "token_contract.c"
#undef main

struct payload { uint64_t token, first, sequence; };
struct request { struct flat_binder_object callback; struct payload payload; };
static void command(int fd, uint32_t cmd, const void *arg, size_t size)
{
    unsigned char bytes[512]; REQUIRE(size+4 <= sizeof(bytes));
    memcpy(bytes,&cmd,4);if(size)memcpy(bytes+4,arg,size);
    struct binder_write_read wr={.write_size=size+4,.write_buffer=(uintptr_t)bytes};
    REQUIRE(ioctl(fd,BINDER_WRITE_READ,&wr)==0 && wr.write_consumed==size+4);
}
static void free_buffer(int fd, binder_uintptr_t buffer)
{ command(fd,BC_FREE_BUFFER,&buffer,sizeof(buffer)); }
static int open_binder(const char *path)
{
    int fd=open(path,O_RDWR|O_CLOEXEC); REQUIRE(fd>=0);
    REQUIRE(mmap(NULL,1024*1024,PROT_READ,MAP_PRIVATE,fd,0)!=MAP_FAILED);
    return fd;
}
static uint32_t receive(int fd, struct binder_transaction_data *tr)
{
    for (;;) {
        unsigned char bytes[1024];
        struct binder_write_read wr={.read_size=sizeof(bytes),.read_buffer=(uintptr_t)bytes};
        REQUIRE(ioctl(fd,BINDER_WRITE_READ,&wr)==0);
        for(size_t off=0;off+4<=wr.read_consumed;) {
            uint32_t cmd;memcpy(&cmd,bytes+off,4);off+=4;
            REQUIRE(off+_IOC_SIZE(cmd)<=wr.read_consumed);
            if(cmd==BR_TRANSACTION || cmd==BR_REPLY) { memcpy(tr,bytes+off,sizeof(*tr));return cmd; }
            if(cmd==BR_INCREFS || cmd==BR_ACQUIRE) {
                struct binder_ptr_cookie pc;memcpy(&pc,bytes+off,sizeof(pc));
                command(fd,cmd==BR_INCREFS?BC_INCREFS_DONE:BC_ACQUIRE_DONE,&pc,sizeof(pc));
            }
            REQUIRE(cmd!=BR_DEAD_REPLY && cmd!=BR_FAILED_REPLY && cmd!=BR_ERROR);
            off+=_IOC_SIZE(cmd);
        }
    }
}
static void check_sender(int fd,uint64_t token,uint64_t first)
{
    struct token_result got={0};
    REQUIRE(ioctl(fd,GET_ACCESS_TOKEN,&got)==0);
    REQUIRE(got.sender==token && got.first==first);
}
static void send_payload(int fd,uint32_t cmd,uint32_t handle,const struct payload *p)
{
    struct binder_transaction_data tr={0};tr.target.handle=handle;tr.code=0xace508;
    tr.data_size=sizeof(*p);tr.data.ptr.buffer=(uintptr_t)p;
    command(fd,cmd,&tr,sizeof(tr));
}
int main(int argc,char **argv)
{
    REQUIRE(argc==3);setbuf(stdout,NULL);alarm(35);
    token_fd=open(argv[1],O_RDWR|O_CLOEXEC);REQUIRE(token_fd>=0);
    int ready[2];REQUIRE(pipe(ready)==0);
    pid_t server=fork();REQUIRE(server>=0);
    if(!server) {
        close(ready[0]);set_token(SERVER,false);
        int fd=open_binder(argv[2]);REQUIRE(ioctl(fd,BINDER_SET_CONTEXT_MGR,0)==0);
        command(fd,BC_ENTER_LOOPER,NULL,0);REQUIRE(write(ready[1],"R",1)==1);close(ready[1]);
        for(unsigned i=0;i<64;i++) {
            struct binder_transaction_data tr;REQUIRE(receive(fd,&tr)==BR_TRANSACTION);
            REQUIRE(tr.data_size==sizeof(struct request));
            struct request req;memcpy(&req,(void *)(uintptr_t)tr.data.ptr.buffer,sizeof(req));
            REQUIRE(req.callback.hdr.type==BINDER_TYPE_HANDLE);
            check_sender(fd,req.payload.token,req.payload.first);
            REQUIRE(get_token(false)==SERVER);
            set_token(req.payload.first,true); /* Same trusted userspace forwarding API as IPC runtime. */
            send_payload(fd,BC_TRANSACTION,req.callback.handle,&req.payload);
            struct binder_transaction_data inner;REQUIRE(receive(fd,&inner)==BR_REPLY);
            check_sender(fd,req.payload.token,req.payload.first);
            REQUIRE(inner.data_size==sizeof(struct payload));
            REQUIRE(!memcmp((void *)(uintptr_t)inner.data.ptr.buffer,&req.payload,sizeof(req.payload)));
            free_buffer(fd,inner.data.ptr.buffer);free_buffer(fd,tr.data.ptr.buffer);
            send_payload(fd,BC_REPLY,0,&req.payload);
        }
        puts("PASS 64 server requests and nested replies carry the correct caller identities");
        _exit(0);
    }
    close(ready[1]);char c;REQUIRE(read(ready[0],&c,1)==1);close(ready[0]);
    pid_t clients[4];
    for(unsigned n=0;n<4;n++) {
        clients[n]=fork();REQUIRE(clients[n]>=0);
        if(!clients[n]) {
            uint64_t self=CLIENT+n,first=FIRST+n;set_token(self,false);set_token(first,true);
            int fd=open_binder(argv[2]);
            if(n==3) {
                REQUIRE(setgroups(0,NULL)==0 && setgid(10000)==0 && setuid(10000)==0);
                uint64_t forged=SERVER;
                REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_TOKENID,&forged)==-1 && errno==EPERM);
                REQUIRE(ioctl(token_fd,ACCESS_TOKENID_SET_FTOKENID,&forged)==-1 && errno==EPERM);
            }
            for(unsigned i=0;i<16;i++) {
                struct request req={.callback={.hdr.type=BINDER_TYPE_BINDER,
                    .flags=FLAT_BINDER_FLAG_ACCEPTS_FDS,.binder=0xace50000,.cookie=0xace50001},
                    .payload={self,first,i}};
                binder_size_t offsets[1]={0};
                struct binder_transaction_data tr={0};tr.target.handle=0;tr.code=0xace507;
                tr.data_size=sizeof(req);tr.data.ptr.buffer=(uintptr_t)&req;
                tr.offsets_size=sizeof(offsets);tr.data.ptr.offsets=(uintptr_t)offsets;
                command(fd,BC_TRANSACTION,&tr,sizeof(tr));
                struct binder_transaction_data incoming;
                REQUIRE(receive(fd,&incoming)==BR_TRANSACTION);check_sender(fd,SERVER,first);
                REQUIRE(incoming.target.ptr==0xace50000 && incoming.cookie==0xace50001);
                REQUIRE(incoming.data_size==sizeof(req.payload));
                REQUIRE(!memcmp((void *)(uintptr_t)incoming.data.ptr.buffer,&req.payload,sizeof(req.payload)));
                free_buffer(fd,incoming.data.ptr.buffer);send_payload(fd,BC_REPLY,0,&req.payload);
                REQUIRE(receive(fd,&incoming)==BR_REPLY);check_sender(fd,SERVER,first);
                REQUIRE(incoming.data_size==sizeof(req.payload));
                REQUIRE(!memcmp((void *)(uintptr_t)incoming.data.ptr.buffer,&req.payload,sizeof(req.payload)));
                free_buffer(fd,incoming.data.ptr.buffer);REQUIRE(get_token(false)==self);
            }
            _exit(0);
        }
    }
    for(unsigned n=0;n<4;n++) wait_ok(clients[n]);
    wait_ok(server);
    puts("PASS synchronous replies, nested callbacks, concurrent caller isolation and unprivileged spoof rejection");
    return 0;
}
