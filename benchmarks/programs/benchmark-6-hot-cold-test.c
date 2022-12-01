#define RUNS 10000000

#include <stdlib.h>


void A(void);
void B(void);

int main(void) {
    A();
    int i = atoi("10");
    B();
}


void A(void) {
    long output = 0; 

    for(int i = 0; i < RUNS; i++) {
        output += i;
    }
}


void B(void) {
    long output = 0; 

    for(int i = 0; i < RUNS; i++) {
        output += i;
    }
}