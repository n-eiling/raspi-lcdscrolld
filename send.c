#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>

#define IPC_FILENAME "/usr/sbin/lcdd"
#define IPC_PROJ_ID 42

#define IPC_SIZE_MSG 1
#define IPC_STR_MSG 2

int send_msg(int msgqid, const char *data, unsigned int size)
{
    int status = -1;
    struct size_msg {
        long mtype;
        unsigned int size;
    } size_m;

    size_m.mtype = IPC_SIZE_MSG;
    size_m.size= size;

    if (msgsnd(msgqid, &size_m, sizeof(int), 0) < 0) {
        printf("could not send size struct\n");
        return -1;
    }
    struct msg {
        long mtype;
        char data[ 1 ];
    } *m = (struct msg *)malloc( size + offsetof( struct msg, data ));

    if( m != NULL ) {
        m->mtype = IPC_STR_MSG;
        memcpy(m->data, data, size);
        if( msgsnd( msgqid, m, size, 0 ) == 0 ) {
            status = 0;
        }
    }

    free( m );

    return status;
}
int ipc_init()
{
    key_t key;
    int msgqid;

    if ((key = ftok(IPC_FILENAME, IPC_PROJ_ID)) == -1)
        return -1;

    if ((msgqid = msgget(key, 0 )) == -1) {
        fprintf(stderr, "msgget failed. maybe message queue doesnt exists\n");
        return -1;
    }

    return msgqid;
}


int main(int argc, const char* argv[])
{
    int msgqid = ipc_init();
    int msglen;
    char* msgbuf;
    if (argc == 3) {
        msglen = strlen(argv[1])+1+strlen(argv[2])+1;
        msgbuf = malloc(msglen);
        strcpy(msgbuf, argv[1]);
        strcpy(msgbuf+strlen(argv[1])+1, argv[2]);
    } else if (argc == 2) {
        msglen = strlen(argv[1])+2;
        msgbuf = malloc(msglen);
        strcpy(msgbuf, argv[1]);
        msgbuf[msglen-1] = '\0';
    } else {
        msglen = strlen("no input.")+1;
        msgbuf = malloc(msglen);
        strcpy(msgbuf, "no input.");
    }
    if (send_msg(msgqid, msgbuf, msglen) != 0) {
        printf("failed to send '%s', '%s' (%u)\n", msgbuf, msgbuf+strlen(msgbuf)+1, msglen);
        return 1;
    } else {
        printf("sent '%s', '%s' (%u)\n", msgbuf, msgbuf+strlen(msgbuf)+1, msglen);
        return 0;
    }


    return 0;
}
