#include "./../include/ttypt/qmap.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Assumindo que QM_STR, QM_HNDL, qmap_open, qmap_put, qmap_save, qmap_close
// e a macro CBUG estejam definidas nos headers incluídos.

#define DB_FILE "test_database.qmap"
#define MAX_ITEMS 5

/**
 * main - Inicializa um qmap, insere dados e salva no disco.
 */
int main(int argc, char *argv[]) {
    uint32_t map_hd;
    int i;
    char key_buf[32];
    uint32_t value_data;

    if (argc < 2 || (strcmp(argv[1], "save") != 0 && strcmp(argv[1], "load") != 0)) {
        fprintf(stderr, "Uso: %s <save|load>\n", argv[0]);
        return 1;
    }

    /* 1. Abrir o Mapa */
    // Abrir o mapa principal para o arquivo.
    // Usamos QM_STR para chaves (strings) e QM_HNDL (uint32_t) para valores.
    map_hd = qmap_open(DB_FILE, "db", QM_STR, QM_HNDL, 0, 0); 
    
    if (map_hd == QM_MISS) {
        fprintf(stderr, "Erro: Não foi possível abrir o qmap.\n");
        return 1;
    }

    printf("Mapa aberto com HD: %u para arquivo: %s\n", map_hd, DB_FILE);
    
    if (strcmp(argv[1], "save") == 0) {
        
        /* 2. Inserir Dados em Memória (Operação 'save') */
        printf("\n--- Operação de SAVE: Inserindo %d itens ---\n", MAX_ITEMS);

        for (i = 1; i <= MAX_ITEMS; i++) {
            // Chave: "KEY_1", "KEY_2", ...
            snprintf(key_buf, sizeof(key_buf), "ENTRY_%02d", i);
            
            // Valor: 1001, 1002, ...
            value_data = 1000 + i; 
            
            // A função qmap_put faz cópias alocadas da chave e do valor.
            qmap_put(map_hd, key_buf, &value_data);
            printf("Inserido: %s -> %u\n", key_buf, value_data);
        }

        /* 3. Salvar o Mapa para o Arquivo */
        printf("\n--- Salvando todos os mapas rastreados no disco ---\n");
        qmap_save();
        printf("Dados salvos com sucesso em '%s'.\n", DB_FILE);

    } else if (strcmp(argv[1], "load") == 0) {
        
        /* 4. Carregar o Mapa do Arquivo (Operação 'load') */
        printf("\n--- Operação de LOAD: Carregando do disco ---\n");
        qmap_load();
        printf("Dados carregados com sucesso de '%s'.\n", DB_FILE);

        /* 5. Verificar Dados Carregados */
        printf("\n--- Verificando itens carregados ---\n");
        const void *key, *value;
        uint32_t cur = qmap_iter(map_hd, NULL, 0);
        int count = 0;

        while (qmap_next(&key, &value, cur)) {
            printf("Recuperado: %s -> %u\n", (const char *)key, *(const uint32_t *)value);
            count++;
        }
        qmap_fin(cur);

        if (count == 0) {
            printf("Aviso: Nenhum item encontrado. O arquivo pode estar vazio ou a operação 'save' não foi executada.\n");
        } else {
            printf("Total de %d itens recuperados.\n", count);
        }
    }
    
    /* 6. Fechar o Mapa e limpar a memória */
    qmap_close(map_hd);
    printf("\nMapa fechado. Memória liberada.\n");

    return 0;
}
