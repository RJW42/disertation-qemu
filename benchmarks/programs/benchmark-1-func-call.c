int function(int a, int b){
    return a + b;
}

int main() {
    long total = 0;

    for(int i = 0; i < 10000; i++) {
        total = function(total, i);
    }

    return total;
}