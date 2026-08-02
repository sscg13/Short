#include <stdio.h>

int main(void)
{
    printf("Hello from a real 16-bit x86!\n");
    printf("sizeof(int)=%d sizeof(long)=%d sizeof(void*)=%d\n",
           (int)sizeof(int), (int)sizeof(long), (int)sizeof(void *));
    return 0;
}
