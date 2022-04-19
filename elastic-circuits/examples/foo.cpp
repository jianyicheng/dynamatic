int foo(int A[10]) {
    int s = 0;
    for (int i = 0; i < 10; i++) {
        int d = A[i];
        if (d > 10)
            s += d * d + 20;
    }
    return s;
}

int main() {
    int A[10];

    for (int i = 0; i < 10; i++)
        A[i] = (i % 2) ? 9 : 11;

    int s = foo(A);

    return 0;
}