/* Newlib syscall ABI. Stream I/O is unavailable in this GPIO-only demo. */
#include <errno.h>
int _close(int fd) {
    (void)fd; errno = ENOSYS; return -1;
}
int _lseek(int fd, int offset, int origin) {
    (void)fd; (void)offset; (void)origin; errno = ENOSYS; return -1;
}
int _read(int fd, char *buffer, int count) {
    (void)fd; (void)buffer; (void)count; errno = ENOSYS; return -1;
}
int _write(int fd, char *buffer, int count) {
    (void)fd; (void)buffer; (void)count; errno = ENOSYS; return -1;
}
