#include <stdio.h>

int main(void)
{
   char buf[4096];
   int x;
   
   while (fgets(buf, sizeof(buf)-2, stdin) != NULL) {
        for (x = 0; x < 128; ) {
            printf("0x%c%c, ", buf[x], buf[x+1]);
            if (!((x += 2) & 31)) printf("\n");
        }
   }
}


/* ref:          */
/* git commit:  b96e96cf8b22a931e8e91098ac37bc72f9e2f033 */
/* commit time: 2023-08-23 14:37:18 +0200 */
