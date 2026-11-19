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

static int ler_endereco(char *linha, size_t tamanho, int primeira, uint32_t *endereco) {
    if (memchr(linha, '\0', tamanho) != NULL) {
        return 0;
    }

    if (primeira && tamanho >= 3 && memcmp(linha, "\xEF\xBB\xBF", 3) == 0) {
        linha += 3;
    }

    while (isspace((unsigned char)*linha)) {
        linha++;
    }

    if (*linha < '0' || *linha > '9') {
        return 0;
    }

    errno = 0;
    char *fim;
    uintmax_t valor = strtoumax(linha, &fim, 10);

    if (errno != 0 || valor > UINT32_MAX) {
        return 0;
    }

    while (isspace((unsigned char)*fim)) {
        fim++;
    }

    if (*fim != '\0') {
        return 0;
    }

    *endereco = (uint32_t)valor;
    return 1;
}

static int ler_enderecos(const char *nome, uint32_t **enderecos, size_t *total) {
    FILE *arquivo = fopen(nome, "r");

    if (arquivo == NULL) {
        perror(nome);
        return 0;
    }

    uint32_t *lista = NULL;
    size_t capacidade = 0;
    size_t usados = 0;
    char *linha = NULL;
    size_t tamanho_linha = 0;
    ssize_t lidos;
    int ok = 1;

    while ((lidos = getline(&linha, &tamanho_linha, arquivo)) >= 0) {
        uint32_t endereco;

        if (!ler_endereco(linha, (size_t)lidos, usados == 0, &endereco)) {
            fprintf(stderr, "Endereco invalido na linha %zu de %s.\n",
                    usados + 1, nome);
            ok = 0;
            break;
        }

        if (usados == capacidade) {
            if (capacidade > SIZE_MAX / sizeof *lista / 2) {
                fprintf(stderr, "Arquivo de enderecos grande demais.\n");
                ok = 0;
                break;
            }

            size_t nova = capacidade == 0 ? 256 : 2 * capacidade;
            uint32_t *ampliada = realloc(lista, nova * sizeof *lista);

            if (ampliada == NULL) {
                perror("realloc");
                ok = 0;
                break;
            }

            lista = ampliada;
            capacidade = nova;
        }

        lista[usados++] = endereco;
    }

    if (ok && (ferror(arquivo) || !feof(arquivo))) {
        fprintf(stderr, "Erro ao ler %s.\n", nome);
        ok = 0;
    }

    if (ok && usados == 0) {
        fprintf(stderr, "O arquivo %s esta vazio.\n", nome);
        ok = 0;
    }

    free(linha);
    fclose(arquivo);

    if (!ok) {
        free(lista);
        return 0;
    }

    *enderecos = lista;
    *total = usados;
    return 1;
}