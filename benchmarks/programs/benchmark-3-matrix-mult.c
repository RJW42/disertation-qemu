#include <stdio.h>
#include <stdlib.h>

#define SEED 123456

typedef struct Matrix {
    int **store;
    int rows;
    int cols;
} Matrix;

Matrix rand_matrix(int rows, int cols);
Matrix mult_matrix(Matrix m1, Matrix m2);
Matrix new_matrix(int rows, int cols);
int mult_rows(Matrix m1, Matrix m2, int r, int c);


int main() {
    srand(SEED);
    Matrix m3;


    for(int i = 0; i < 10; i++) {
        Matrix m1 = rand_matrix(10, 10);
        Matrix m2 = rand_matrix(10, 10);
        m3 = mult_matrix(m1, m2);
    }

    return 0;
}


Matrix mult_matrix(Matrix m1, Matrix m2) {
    if(m1.cols != m2.rows) {
        perror("Rows and Cols don't match");
        exit(EXIT_FAILURE);
    }

    Matrix out = new_matrix(m1.rows, m2.cols);

    for(int r = 0; r < out.rows; r++) {
        for(int c = 0; c < out.cols; c++) {
            out.store[r][c] = mult_rows(m1, m2, r, c);
        }
    }

    return out;
}


Matrix rand_matrix(int rows, int cols) {
    Matrix mat = new_matrix(rows, cols);

    for(int r = 0; r < rows; r++) {
        for(int c = 0; c < cols; c++) {
            mat.store[r][c] = rand() % 1000;
        }
    }

    return mat;
}


Matrix new_matrix(int rows, int cols) {
    Matrix mat = {
        .store = calloc(sizeof(int*), rows),
        .rows = rows,
        .cols = cols
    };

    for(int r = 0; r < rows; r++)
        mat.store[r] = calloc(sizeof(int), cols);

    return mat;
}

int mult_rows(Matrix m1, Matrix m2, int r, int c) {
    int res = 0; 

    for(int i = 0; i < m1.cols; i++) {
        res += (m1.store[r][i] * m2.store[i][c]) % 1000;
    }

    return res;
}