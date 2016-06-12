#define LOG_BUFF_SZ 0x400
// error codes
/* system error */
#define RUN_FROM_CHROOT_CANT_OPEN                   1
#define RUN_FROM_CHROOT_CANT_WRITE                  1
#define RUN_FROM_CHROOT_CANT_CHROOT                 1
#define RUN_FROM_CHROOT_CANT_CHDIR                  1
/* command execution error */
#define RUN_FROM_CHROOT_CANT_EXEC                   3
/* internal error */
#define RUN_FROM_CHROOT_INTERNAL                    2
#define RUN_FROM_CHROOT_CANT_FORK                   1
/* running command exit with non-null code */
#define RUN_FROM_CHROOT_CMD_FAILED                  5
/* program terminated by signal */
#define RUN_FROM_CHROOT_PROG_SIGNALED               7
