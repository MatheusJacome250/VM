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

static void iniciar(simulador *vm) {
    for (int i = 0; i < NUM_PAGINAS; i++) {
        vm->tabela_paginas[i] = -1;
    }

    for (int i = 0; i < TAM_TLB; i++) {
        vm->tlb[i].pagina = -1;
    }
}

static size_t prioridade(historico tempo, politica escolha) {
    return escolha == FIFO ? tempo.chegada : tempo.ultimo_uso;
}

static int escolher_quadro(const simulador *vm, politica escolha) {
    if (vm->quadros_usados < NUM_QUADROS) {
        return vm->quadros_usados;
    }

    int vitima = 0;

    for (int i = 1; i < NUM_QUADROS; i++) {
        if (prioridade(vm->quadros[i].tempo, escolha) <
            prioridade(vm->quadros[vitima].tempo, escolha)) {
            vitima = i;
        }
    }

    return vitima;
}

static int carregar_pagina(simulador *vm, FILE *binario, int pagina, politica escolha) {
    int posicao = escolher_quadro(vm, escolha);
    long posicao_arquivo = (long)pagina * TAM_PAGINA;

    if (fseek(binario, posicao_arquivo, SEEK_SET) != 0 ||
        fread(vm->ram[posicao], 1, TAM_PAGINA, binario) != TAM_PAGINA) {
        fprintf(stderr, "Erro ao ler a pagina %d do BACKING_STORE.bin.\n", pagina);
        return -1;
    }

    if (vm->quadros_usados < NUM_QUADROS) {
        vm->quadros_usados++;
    } else {
        int removida = vm->quadros[posicao].pagina;
        vm->tabela_paginas[removida] = -1;

        for (int i = 0; i < TAM_TLB; i++) {
            if (vm->tlb[i].pagina == removida) {
                vm->tlb[i].pagina = -1;
                break;
            }
        }
    }

    vm->tabela_paginas[pagina] = posicao;
    vm->quadros[posicao] = (quadro){pagina, {vm->acessos, vm->acessos}};
    return posicao;
}

static void *comparar_entrada(void *arg) {
    tarefa_tlb *tarefa = arg;
    tarefa->achou = tarefa->entrada->pagina == tarefa->pagina;
    return NULL;
}

static int buscar_tlb(const simulador *vm, int pagina, int *indice) {
    pthread_t threads[TAM_TLB];
    tarefa_tlb tarefas[TAM_TLB];
    int criadas = 0;

    for (; criadas < TAM_TLB; criadas++) {
        tarefas[criadas] = (tarefa_tlb){&vm->tlb[criadas], pagina, 0};

        int erro = pthread_create(&threads[criadas], NULL, comparar_entrada,
                                  &tarefas[criadas]);
        if (erro != 0) {
            fprintf(stderr, "Erro ao criar thread da TLB: %s.\n", strerror(erro));
            break;
        }
    }

    for (int i = 0; i < criadas; i++) {
        int erro = pthread_join(threads[i], NULL);

        if (erro != 0) {
            fprintf(stderr, "Erro ao aguardar thread da TLB: %s.\n", strerror(erro));
            exit(EXIT_FAILURE);
        }
    }

    if (criadas != TAM_TLB) {
        return 0;
    }

    *indice = -1;

    for (int i = 0; i < TAM_TLB; i++) {
        if (tarefas[i].achou) {
            *indice = i;
            break;
        }
    }

    return 1;
}

static int inserir_tlb(simulador *vm, int pagina, int posicao, politica escolha) {
    int indice = 0;

    for (int i = 0; i < TAM_TLB; i++) {
        if (vm->tlb[i].pagina == -1) {
            indice = i;
            break;
        }

        if (prioridade(vm->tlb[i].tempo, escolha) <
            prioridade(vm->tlb[indice].tempo, escolha)) {
            indice = i;
        }
    }

    vm->tlb[indice] = (entrada_tlb){pagina, posicao, {vm->acessos, vm->acessos}};
    return indice;
}

static int traduzir(simulador *vm, FILE *binario, FILE *saida, uint32_t endereco, politica politica_ram, politica politica_tlb) {
    int pagina = (int)((endereco >> 8) & 0xFFu);
    int deslocamento = (int)(endereco & 0xFFu);
    int indice;
    int posicao;

    vm->acessos++;

    if (!buscar_tlb(vm, pagina, &indice)) {
        return 0;
    }

    if (indice != -1) {
        vm->acertos_tlb++;
        posicao = vm->tlb[indice].quadro;
        vm->tlb[indice].tempo.ultimo_uso = vm->acessos;
    } else {
        posicao = vm->tabela_paginas[pagina];

        if (posicao == -1) {
            vm->faltas++;
            posicao = carregar_pagina(vm, binario, pagina, politica_ram);

            if (posicao == -1) {
                return 0;
            }
        }

        indice = inserir_tlb(vm, pagina, posicao, politica_tlb);
    }

    vm->quadros[posicao].tempo.ultimo_uso = vm->acessos;
    int fisico = posicao * TAM_PAGINA + deslocamento;
    int valor = vm->ram[posicao][deslocamento];

    if (valor >= 128) {
        valor -= 256;
    }

    if (fprintf(saida, "Virtual address: %" PRIu32
                " TLB: %d Physical address: %d Value: %d\n",
                endereco, indice, fisico, valor) < 0) {
        perror("correct.txt");
        return 0;
    }

    return 1;
}

static int escrever_estatisticas(FILE *saida, const simulador *vm) {
    return fprintf(saida,
                   "Number of Translated Addresses = %zu\n"
                   "Page Faults = %zu\n"
                   "Page Fault Rate = %.3f\n"
                   "TLB Hits = %zu\n"
                   "TLB Hit Rate = %.3f",
                   vm->acessos, vm->faltas, (double)vm->faltas / vm->acessos,
                   vm->acertos_tlb, (double)vm->acertos_tlb / vm->acessos) >= 0;
}

int main(int argc, char *argv[]) {
    politica politica_ram;
    politica politica_tlb;

    if (argc != 4 || !ler_politica(argv[2], &politica_ram) ||
        !ler_politica(argv[3], &politica_tlb)) {
        fprintf(stderr, "Uso: %s arquivo_enderecos fifo|lru fifo|lru\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint32_t *enderecos;
    size_t total;

    if (!ler_enderecos(argv[1], &enderecos, &total)) {
        return EXIT_FAILURE;
    }

    FILE *binario = fopen("BACKING_STORE.bin", "rb");

    if (binario == NULL) {
        perror("BACKING_STORE.bin");
        free(enderecos);
        return EXIT_FAILURE;
    }

    FILE *saida = fopen("correct.txt", "wb");

    if (saida == NULL) {
        perror("correct.txt");
        fclose(binario);
        free(enderecos);
        return EXIT_FAILURE;
    }

    simulador vm = {0};
    iniciar(&vm);

    int ok = 1;

    for (size_t i = 0; ok && i < total; i++) {
        ok = traduzir(&vm, binario, saida, enderecos[i],
                      politica_ram, politica_tlb);
    }

    if (ok && !escrever_estatisticas(saida, &vm)) {
        perror("correct.txt");
        ok = 0;
    }

    if (fclose(saida) == EOF) {
        perror("correct.txt");
        ok = 0;
    }

    fclose(binario);
    free(enderecos);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}