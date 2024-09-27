#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void f(int parent_fd[])
{
    close(parent_fd[1]);
    int p;
    int ret_val = read(parent_fd[0], &p, sizeof(int));
    if (ret_val == 0)
    {
        close(parent_fd[0]);
        exit(0);
    }
    printf("prime %d\n", p);

    int cur_fd[2];
    pipe(cur_fd);

    int id = fork();
    if (id == 0)
    {
        close(parent_fd[0]);
        f(cur_fd);
    }
    else if (id > 0)
    {
        close(cur_fd[0]);
        int n;
        while (read(parent_fd[0], &n, sizeof(int)) != 0)
        {
            if (n % p)
            {
                write(cur_fd[1], &n, sizeof(int));
            }
        }
        close(cur_fd[1]);
        close(parent_fd[0]);
        wait(0);
    }
}
int main(int argc, char *argv[])
{
    int fd[2];
    pipe(fd);

    int id = fork();
    if (id == 0)
    {
        f(fd);
    }
    else if (id > 0)
    {
        close(fd[0]);
        for (int i = 2; i < 36; ++i)
        {
            // send to right process
            write(fd[1], &i, sizeof(int));
        }
        close(fd[1]);
        wait(0);
    }
    exit(0);
}