int main() {
    int a1 = 0;
    int a2 = 1;

    while(a2 < 10000000) {
        int tmp = a2;
        a2 = a1 + a2; 
        a1 = a2;
    }

    return 0;
}