# Simulador de memória virtual

Programa em C que traduz endereços virtuais em endereços físicos. Simula uma memória física com 128 quadros de 256 bytes e uma TLB com 16 entradas. A busca na TLB usa uma thread por entrada.

## Compilação

Na pasta do projeto, rode `make` para gerar o executável `vm`. É necessário um compilador C com suporte a POSIX Threads.

## Execução

Coloque o `BACKING_STORE.bin` na pasta de onde o programa será executado. O arquivo de entrada deve ter um endereço decimal por linha.

```bash
./vm <arquivo_de_enderecos> <politica_de_paginas> <politica_da_tlb>
```

Cada política aceita `fifo` ou `lru`. A primeira controla a substituição de páginas na memória física. A segunda controla a substituição de entradas na TLB.

Para reproduzir o teste do professor, rode:

```bash
./vm addresses.txt lru fifo
```

A saída é gravada em `correct.txt` e inclui as traduções, os valores lidos, as faltas de página e os acertos na TLB. No teste do professor, o resultado esperado é de 1.000 endereços traduzidos, 539 faltas de página e 54 acertos na TLB.

## Limpeza

Rode `make clean` para remover o executável.

## Referência

*Operating System Concepts*, 10ª edição, capítulo 10, projeto *Designing a Virtual Memory Manager*.