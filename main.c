#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TAM_PAGINA = 256,
    NUM_PAGINAS = 256,
    NUM_QUADROS = 128,
    TAM_TLB = 16
};

typedef enum { FIFO, LRU } politica;

typedef struct {
    size_t chegada;
    size_t ultimo_uso;
} historico;

typedef struct {
    int pagina;
    historico tempo;
} quadro;

typedef struct {
    int pagina;
    int quadro;
    historico tempo;
} entrada_tlb;

typedef struct {
    int tabela_paginas[NUM_PAGINAS];
    unsigned char ram[NUM_QUADROS][TAM_PAGINA];
    quadro quadros[NUM_QUADROS];
    entrada_tlb tlb[TAM_TLB];
    int quadros_usados;
    size_t acessos;
    size_t faltas;
    size_t acertos_tlb;
} simulador;

typedef struct {
    const entrada_tlb *entrada;
    int pagina;
    int achou;
} tarefa_tlb;

static int ler_politica(const char *texto, politica *escolha) {
    if (strcmp(texto, "fifo") == 0) {
        *escolha = FIFO;
        return 1;
    }

    if (strcmp(texto, "lru") == 0) {
        *escolha = LRU;
        return 1;
    }

    return 0;
}

