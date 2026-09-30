#include <stdio.h>

#define START_VALUE 1

int factorial(int n)
{
    int i;
    int result = START_VALUE;

    i = 2;

    while (i <= n)
    {
        result = result * i;
        i = i + 1;
    }

    return result;
}

int main(void)
{
    int n;
    int result;

    printf("Input n: ");
    scanf("%d", &n);

    result = factorial(n);

    printf("factorial = %d\n", result);

    return 0;
}