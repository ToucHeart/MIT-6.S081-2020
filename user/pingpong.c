#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//pipe is single direction,one end read and one end write 

int main(int argc, char *argv[]) {
  int p[2];
  int q[2];
  pipe(p);
  pipe(q);
  int id = fork();
  if (id == 0) {
    close(p[1]);
    close(q[0]);
    char a;
    read(p[0], &a, 1);
    printf("%d: received ping\n", getpid());
    write(q[1], &a, 1);
    exit(0);
  } else if (id > 0) {
    close(p[0]);
    close(q[1]);
    write(p[1], "c", 1);
    char a;
    read(q[0], &a, 1);
    printf("%d: received pong\n", getpid());
    exit(0);
  }
  exit(0);
}